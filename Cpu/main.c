#include "main.h"

archivoConfigCPU* t_archivoConfig;
t_config *config;
struct sockaddr_in direccionKernel;
int32_t cliente;
char* buffer;
int valorVaribleCompartida;
struct sockaddr_in direccionMem;
int32_t clienteMEM;
int32_t bytesRecibidos;
int32_t header;
programControlBlock *unPcb;
int32_t tamanoPag;
pthread_t hiloKernel;
pthread_t hiloMemoria;
pthread_t hiloProcesarScript;
pthread_mutex_t mutexProcesar;
sem_t semProcesar;
sem_t semInstruccion;
sem_t semSentenciaCompleta;
sem_t semHayScript;
sem_t semEscribirVariable;
sem_t semDereferenciar;
sem_t semDestruirPCB;
sem_t semVariableCompartidaValor;
sem_t semProcesoBloqueado;
int noInteresa;
int valorDerenferenciado;
int algoritmo;
int quantum;
int quantumSleep;
int stackSize;
char * instruccionLeida;
AnSISOP_funciones primitivas = { .AnSISOP_definirVariable = definirVariable,
		.AnSISOP_obtenerPosicionVariable = obtenerPosicionVariable,
		.AnSISOP_dereferenciar = dereferenciar, .AnSISOP_asignar = asignar,
		.AnSISOP_finalizar = finalizar, .AnSISOP_obtenerValorCompartida =
				obtenerValorCompartida, .AnSISOP_asignarValorCompartida =
				asignarValorCompartida, .AnSISOP_irAlLabel = irAlLabel,
		.AnSISOP_retornar = retornar, .AnSISOP_llamarConRetorno =
				llamarConRetorno };
AnSISOP_kernel privilegiadas = { .AnSISOP_escribir = escribir, .AnSISOP_wait =
		wait_kernel, .AnSISOP_signal = signal_kernel };

int32_t main(int argc, char**argv) {
	Configuracion(argv[1]);
	pthread_create(&hiloKernel, NULL, ConectarConKernel, NULL);
	pthread_create(&hiloMemoria, NULL, conectarConMemoria, NULL);
	pthread_create(&hiloProcesarScript, NULL, procesarScript, NULL);

	pthread_join(hiloKernel, NULL);
	pthread_join(hiloMemoria, NULL);
	pthread_join(hiloProcesarScript, NULL);
	return EXIT_SUCCESS;
}
void Configuracion(char* dir) {
	t_archivoConfig = malloc(sizeof(archivoConfigCPU));
	configuracionCpu(t_archivoConfig, config, dir);
	sem_init(&semProcesar, 0, 1);
	sem_init(&semSentenciaCompleta, 0, 0);
	sem_init(&semInstruccion, 0, 0);
	sem_init(&semHayScript, 0, 0);
	sem_init(&semEscribirVariable, 0, 0);
	sem_init(&semDereferenciar, 0, 0);
	sem_init(&semDestruirPCB, 0, 1);
	sem_init(&semVariableCompartidaValor, 0, 0);
	sem_init(&semProcesoBloqueado, 0, 0);
}

int32_t conectarConMemoria() {
	llenarSocketAdrrConIp(&direccionMem, t_archivoConfig->IP_MEMORIA,
			t_archivoConfig->PUERTO_MEMORIA);
	clienteMEM = socket(AF_INET, SOCK_STREAM, 0);
	if (connect(clienteMEM, (void*) &direccionMem, sizeof(direccionMem)) != 0) {
		perror("No se pudo conectar");
		return 1;
	}
	Serializar(CPU, 4, &noInteresa, clienteMEM);
	while (1) {
		//sem_wait(&semProcesar);
		paquete* paqueteRecibido = Deserializar(clienteMEM);
		if (paqueteRecibido->header < 0) {
			perror("Memoria se desconectó");
			return 1;
		}
		procesar(paqueteRecibido->package, paqueteRecibido->header,
				paqueteRecibido->size);
	}
}

int32_t ConectarConKernel() {
	llenarSocketAdrrConIp(&direccionKernel, t_archivoConfig->IP_KERNEL,
			t_archivoConfig->PUERTO_KERNEL);

	cliente = socket(AF_INET, SOCK_STREAM, 0);
	if (connect(cliente, (void*) &direccionKernel, sizeof(direccionKernel))
			!= 0) {
		perror("No se pudo conectar");
		return 1;
	}
	Serializar(CPU, 4, &noInteresa, cliente);

	while (1) {
		paquete* paqueteRecibido = Deserializar(cliente);
		if (paqueteRecibido->header < 0) {
			perror("Kernel se desconectó");
			return 1;
		}
		procesar(paqueteRecibido->package, paqueteRecibido->header,
				paqueteRecibido->size);
	}

	free(buffer);
}
void procesar(char * paquete, int32_t id, int32_t tamanoPaquete) {
	switch (id) {
	case ARCHIVO: {
		printf("%s", paquete);
		break;
	}
	case FILESYSTEM: {
		printf("Se conecto FS");
		break;
	}
	case KERNEL: {
		printf("Se conecto Kernel\n");
		break;
	}
	case CPU: {
		printf("Se conecto CPU");
		break;
	}
	case CONSOLA: {
		printf("Se conecto Consola");
		break;
	}
	case VALORVARIABLECOMPARTIDA: {
		memcpy(&valorVaribleCompartida, paquete, 4);
		printf("compartida en procesar %d", valorVaribleCompartida);
		sem_post(&semVariableCompartidaValor);
		break;
	}
	case MEMORIA: {
		memcpy(&tamanoPag, (paquete), sizeof(int));
		printf("Se conecto Memoria\n");
		break;
	}
	case LEERSENTENCIA: {
		instruccionLeida = malloc(tamanoPaquete);
		memcpy(instruccionLeida, paquete, tamanoPaquete);
		sem_post(&semInstruccion);
		break;
	}
	case ESCRIBIRVARIABLE: {
		sem_post(&semEscribirVariable);
		break;
	}
	case DEREFERENCIAR: {
		memcpy(&valorDerenferenciado, paquete, 4);
		sem_post(&semDereferenciar);
		break;
	}
	case PROCESOWAIT: {
		memcpy(&programaBloqueado, paquete, 4);
		sem_post(&semProcesoBloqueado);
		break;
	}
	case PCB: {
		sem_wait(&semDestruirPCB);
		unPcb = deserializarPCB(paquete);
		printf("unPcb id: %d\n", unPcb->programId);
		//sleep(1000000);
		sem_post(&semHayScript);
		break;
	}
	case DATOSKERNELCPU: {
		memcpy(&quantum, paquete, 4);
		memcpy(&quantumSleep, paquete + 4, 4);
		memcpy(&algoritmo, paquete + 8, 4);
		memcpy(&stackSize, paquete + 12, 4);
		printf("quatum: %d\n", quantum);
		printf("quatum slep: %d\n", quantumSleep);
		printf("stack: %d\n", stackSize);
		printf("algoritmo: %d\n", algoritmo);
	}
	}

}

void procesarScript() {
	while (1) {
		sem_wait(&semHayScript);
		programaBloqueado = 0;
		programaFinalizado = 0;
		programaAbortado = 0;
		int quantum_aux = quantum;
		int pid = unPcb->programId;
		while ((quantum_aux != 0) && !programaBloqueado && !programaFinalizado
				&& !programaAbortado) {
			posicionMemoria* datos_para_memoria = malloc(
					sizeof(posicionMemoria));
			crearEstructuraParaMemoria(unPcb, tamanoPag, datos_para_memoria);
			char* sentencia = leerSentencia(datos_para_memoria->pag,
					datos_para_memoria->off, datos_para_memoria->size, 0);
			sem_wait(&semSentenciaCompleta);
			char* barra_cero = "\0";
			memcpy(sentencia + (datos_para_memoria->size - 1), barra_cero, 1);
			printf("[procesarScript]Sentencia: %s de pid %d \n", sentencia,
					pid);
			analizadorLinea(depurarSentencia(sentencia), &primitivas,
					&privilegiadas);
			free(datos_para_memoria);
			free(sentencia);
			unPcb->programCounter++;
			quantum_aux--;
			usleep(quantumSleep * 1000);
		}
		if (programaBloqueado) {
			serializarPCB(unPcb, cliente, SEBLOQUEOELPROCESO);
			destruirPCB(unPcb);
		}
		if ((quantum_aux == 0) && !programaFinalizado && !programaBloqueado
				&& !programaAbortado) {

			serializarPCB(unPcb, cliente, FINDEQUATUM);
			destruirPCB(unPcb);
			sem_post(&semDestruirPCB);
		}
		//Serializar(PROGRAMATERMINADO, 4, &noInteresa, cliente);
	}
}

char* depurarSentencia(char* sentencia) {

	int i = strlen(sentencia);
	while (string_ends_with(sentencia, "\n")) {
		i--;
		sentencia = string_substring_until(sentencia, i);
	}
	return sentencia;

}
t_puntero definirVariable(t_nombre_variable nombreVariable) {
	posicionMemoria *direccionVariable = malloc(sizeof(posicionMemoria));
	variable *unaVariable = malloc(sizeof(variable));
	indiceDeStack *indiceStack = malloc(sizeof(indiceDeStack));
	indiceStack = (indiceDeStack*) (list_get(unPcb->indiceStack,
			unPcb->tamanoIndiceStack - 1));

	if (unPcb->tamanoIndiceStack == 1 && indiceStack->tamanoVars == 0) {

		armarDireccionPrimeraPagina(direccionVariable);
		unaVariable->etiqueta = nombreVariable;
		unaVariable->direccion = direccionVariable;
		list_add(indiceStack->vars, unaVariable);
		indiceStack->pos = 0;
		indiceStack->tamanoVars++;
	} else {
		armarProximaDireccion(direccionVariable);
		unaVariable->etiqueta = nombreVariable;
		unaVariable->direccion = direccionVariable;
		list_add(indiceStack->vars, unaVariable);
		indiceStack->tamanoVars++;
	}
	int valor = 0;
	int direccionRetorno = convertirDireccionAPuntero(direccionVariable);
	printf("[definirVariable]Defino %c ubicada en %d\n", nombreVariable,
			direccionRetorno);
	enviarDirecParaEscribirMemoria(direccionVariable, valor);
	return (direccionRetorno);

}

t_puntero obtenerPosicionVariable(t_nombre_variable nombreVariable) {
	printf("[obtenerPosicionVariable]Obtener posicion de %c\n", nombreVariable);
	int posicionStack = unPcb->tamanoIndiceStack - 1;
	int direccionRetorno;
	variable *variableNueva;
	int posMax =
			(((indiceDeStack*) (list_get(unPcb->indiceStack, posicionStack)))->tamanoVars)
					- 1;
	while (posMax >= 0) {
		variableNueva =
				((variable*) (list_get(
						((indiceDeStack*) (list_get(unPcb->indiceStack,
								posicionStack)))->vars, posMax)));
		printf("[obtenerPosicionVariable]Variable: %c\n",
				variableNueva->etiqueta);
		if (variableNueva->etiqueta == nombreVariable) {
			direccionRetorno =
					convertirDireccionAPuntero(
							((variable*) (list_get(
									((indiceDeStack*) (list_get(
											unPcb->indiceStack, posicionStack)))->vars,
									posMax)))->direccion);
			printf("[obtenerPosicionVariable]Obtengo valor de %c: %d %d %d\n",
					variableNueva->etiqueta, variableNueva->direccion->pag,
					variableNueva->direccion->off,
					variableNueva->direccion->size);
			return (direccionRetorno);
		}
		posMax--;
	}
	printf("No debería llegar aca\n");
}

void finalizar(void) {
	indiceDeStack *contextoAFinalizar;
	contextoAFinalizar = list_get(unPcb->indiceStack,
			unPcb->tamanoIndiceStack - 1);

	while (contextoAFinalizar->tamanoVars != 0) {
		variable * variableABorrar = (variable *) list_get(
				contextoAFinalizar->vars, contextoAFinalizar->tamanoVars - 1);
		free(
				(posicionMemoria *) ((variable *) list_get(
						contextoAFinalizar->vars,
						contextoAFinalizar->tamanoVars - 1))->direccion);
		free(
				list_get(contextoAFinalizar->vars,
						contextoAFinalizar->tamanoVars - 1));
		contextoAFinalizar->tamanoVars--;
	}
	list_destroy(contextoAFinalizar->vars);

	while (contextoAFinalizar->tamanoArgs != 0) {
		free(
				(posicionMemoria*) list_get(contextoAFinalizar->args,
						contextoAFinalizar->tamanoArgs - 1));
		contextoAFinalizar->tamanoArgs--;
	}
	list_destroy(contextoAFinalizar->args);
	free(contextoAFinalizar);
	unPcb->tamanoIndiceStack--;
	printf("[finalizar]Programa Finalizado / Contexto Destruido\n");
	programaFinalizado = 1;

	Serializar(PROGRAMATERMINADO, 4, &noInteresa, cliente);
	destruirPCB(unPcb);
	sem_post(&semDestruirPCB);
}

void llamarConRetorno(t_nombre_etiqueta etiqueta, t_puntero punteroRetorno) {
	posicionMemoria *direccionRetorno = malloc(sizeof(posicionMemoria));
	convertirPunteroADireccion(punteroRetorno, direccionRetorno);
	int posicionStack = unPcb->tamanoIndiceStack;
	printf("[llamarConRetorno]Tamanio contexto actual %d\n",
			unPcb->tamanoIndiceStack);
	indiceDeStack *nuevoContexto = malloc(sizeof(indiceDeStack));
	nuevoContexto->pos = posicionStack;
	nuevoContexto->args = list_create();
	nuevoContexto->vars = list_create();
	nuevoContexto->tamanoArgs = 0;
	nuevoContexto->tamanoVars = 0;
	memcpy(&nuevoContexto->retPos, &unPcb->programCounter, 4);
	memcpy(&nuevoContexto->retVar, direccionRetorno, sizeof(posicionMemoria));
	printf(
			"[llamarConRetorno]Creo nuevo contexto con pos: %d que debe volver en la sentencia %d y retorno en la variable de pos %d %d\n",
			nuevoContexto->pos, nuevoContexto->retPos,
			nuevoContexto->retVar.pag, nuevoContexto->retVar.off);
	list_add(unPcb->indiceStack, nuevoContexto);
	unPcb->tamanoIndiceStack++;
	irAlLabel(etiqueta);
}

void irAlLabel(t_nombre_etiqueta etiqueta) {
	t_puntero_instruccion instruccion;
	printf("[irAlLabel]Busco etiqueta: %s y mide: %d\n", etiqueta,
			strlen(etiqueta));
	instruccion = metadata_buscar_etiqueta(etiqueta, unPcb->indiceEtiquetas,
			unPcb->tamanoindiceEtiquetas);
	printf("[irAlLabel]Ir a instruccion %d\n", instruccion);
	unPcb->programCounter = instruccion - 1;
	printf("[irAlLabel]Saliendo de label\n");
	return;
}

void retornar(t_valor_variable valorRetorno) {
	int posConextoActual = unPcb->tamanoIndiceStack - 1;
	int direccionRetorno;
	indiceDeStack *contextoAFinalizar = list_get(unPcb->indiceStack,
			posConextoActual);
	direccionRetorno = convertirDireccionAPuntero(
			&(contextoAFinalizar->retVar));
	asignar(direccionRetorno, valorRetorno);
	printf("[retornar]Retornando %d en %d\n", valorRetorno, direccionRetorno);

	unPcb->programCounter = contextoAFinalizar->retPos;

	while (contextoAFinalizar->tamanoVars != 0) {
		free(
				((variable*) list_get(contextoAFinalizar->vars,
						contextoAFinalizar->tamanoVars - 1))->direccion);
		free(
				list_get(contextoAFinalizar->vars,
						contextoAFinalizar->tamanoVars - 1));
		contextoAFinalizar->tamanoVars--;
	}
	list_destroy(contextoAFinalizar->vars);
	printf("[retornar]Destrui vars de funcion\n");

	while (contextoAFinalizar->tamanoArgs != 0) {
		free(
				(posicionMemoria*) list_get(contextoAFinalizar->args,
						contextoAFinalizar->tamanoArgs - 1));
		contextoAFinalizar->tamanoArgs--;
	}
	list_destroy(contextoAFinalizar->args);
	printf("[retornar]Destrui args de funcion\n");
	free(list_get(unPcb->indiceStack, unPcb->tamanoIndiceStack - 1));
	printf("[retornar]Contexto Finalizado\n");
	unPcb->tamanoIndiceStack--;
}

bool terminoElPrograma(void) {

	return programaFinalizado;
}

t_valor_variable dereferenciar(t_puntero puntero) {
	posicionMemoria *direccion = malloc(sizeof(posicionMemoria));
	convertirPunteroADireccion(puntero, direccion);
	enviarDirecParaLeerMemoria(direccion, DEREFERENCIAR);
	sem_wait(&semDereferenciar);
	free(direccion);
	printf("[dereferenciar]Dereferenciar %d y su valor es: %d\n", puntero,
			valorDerenferenciado);
	return valorDerenferenciado;
}

void asignar(t_puntero punteroAVariable, t_valor_variable valor) {
	printf("[asignar]Asignando en %d el valor %d\n", punteroAVariable, valor);
	posicionMemoria *direccion = malloc(sizeof(posicionMemoria));
	convertirPunteroADireccion(punteroAVariable, direccion);
	//ARREGLAR INAKI
	enviarDirecParaEscribirMemoria(direccion, valor);
	free(direccion);
	return;
}

void armarDireccionPrimeraPagina(posicionMemoria *direccionReal) {
	posicionMemoria *direccion = malloc(sizeof(posicionMemoria));
	direccion->off = 0;
	direccion->size = 4;
	direccion->pag = primeraPagina();
	memcpy(direccionReal, direccion, sizeof(posicionMemoria));
	free(direccion);

	return;
}

int primeraPagina() {
	return unPcb->cantidadDePaginas + 1;
}

void armarProximaDireccion(posicionMemoria* direccionReal) {
	int ultimaPosicionStack = unPcb->tamanoIndiceStack - 1;
	int posicionUltimaVariable = ((indiceDeStack*) (list_get(unPcb->indiceStack,
			ultimaPosicionStack)))->tamanoVars - 1;
	proximaDireccion(ultimaPosicionStack, posicionUltimaVariable,
			direccionReal);
	return;
}

void proximaDireccion(int posStack, int posUltVar,
		posicionMemoria* direccionReal) {
	posicionMemoria *direccion = malloc(sizeof(posicionMemoria));
	int offset = ((variable*) (list_get(
			((indiceDeStack*) (list_get(unPcb->indiceStack, posStack)))->vars,
			posUltVar)))->direccion->off + 4;
	if (offset >= tamanoPag) {
		direccion->pag =
				((variable*) (list_get(
						((indiceDeStack*) (list_get(unPcb->indiceStack,
								posStack)))->vars, posUltVar)))->direccion->pag
						+ 1;
		direccion->off = 0;
		direccion->size = 4;
		memcpy(direccionReal, direccion, sizeof(posicionMemoria));
		free(direccion);
	} else {
		direccion->pag =
				((variable*) (list_get(
						((indiceDeStack*) (list_get(unPcb->indiceStack,
								posStack)))->vars, posUltVar)))->direccion->pag;
		direccion->off = offset;
		direccion->size = 4;
		memcpy(direccionReal, direccion, sizeof(posicionMemoria));
		free(direccion);
	}

	return;
}

void enviarDirecParaEscribirMemoria(posicionMemoria* direccion, int valor) {
	char* variableAEnviar = malloc(20);
	memcpy(variableAEnviar, &direccion->pag, 4);
	memcpy(variableAEnviar + 4, &direccion->off, 4);
	memcpy(variableAEnviar + 8, &direccion->size, 4);
	memcpy(variableAEnviar + 12, &valor, 4);
	memcpy(variableAEnviar + 16, &unPcb->programId, 4);
	printf(
			"[enviarDirecParaEscribirMemoria]Quiero escribir en la direccion %d %d %d el valor: %d\n",
			((int*) (variableAEnviar))[0], ((int*) (variableAEnviar))[1],
			((int*) (variableAEnviar))[2], ((int*) (variableAEnviar))[3]);
	Serializar(ESCRIBIRVARIABLE, 20, variableAEnviar, clienteMEM);
	sem_wait(&semEscribirVariable);
	free(variableAEnviar);
	//paquete * paquetin;
	//paquetin = Deserializar(clienteMEM);
	//liberarPaquete(paquetin);

}

void enviarDirecParaLeerMemoria(posicionMemoria* direccion, int header) {
	char * variableALeer = malloc(16);
	memcpy(variableALeer, &direccion->pag, 4);
	memcpy(variableALeer + 4, &direccion->off, 4);
	memcpy(variableALeer + 8, &direccion->size, 4);
	memcpy(variableALeer + 12, &unPcb->programId, 4);
	printf(
			"[enviarDirecParaLeerMemoria]Quiero leer en la direccion: %d %d %d\n",
			((int*) (variableALeer))[0], ((int*) (variableALeer))[1],
			((int*) (variableALeer))[2]);
	Serializar(header, 16, variableALeer, clienteMEM);
	free(variableALeer);

}

int convertirDireccionAPuntero(posicionMemoria* direccion) {

	int direccion_real, pagina, offset;
	pagina = (direccion->pag) * tamanoPag;
	offset = direccion->off;
	direccion_real = pagina + offset;
	return direccion_real;
}

void convertirPunteroADireccion(int puntero, posicionMemoria* direccion) {
	if (tamanoPag > puntero) {
		direccion->pag = 0;
		direccion->off = puntero;
		direccion->size = 4;
	} else {
		direccion->pag = (puntero / tamanoPag);
		direccion->off = puntero % tamanoPag;
		direccion->size = 4;
	}
	return;
}

void crearEstructuraParaMemoria(programControlBlock* unPcb, int tamPag,
		posicionMemoria* informacion) {

	posicionMemoria* info = malloc(sizeof(posicionMemoria));
	info->pag = ceil(
			(double) unPcb->indiceCodigo[(unPcb->programCounter) * 2]
					/ (double) tamPag);
	//printf("[crearEstructuraParaMemoria]Voy a leer la pagina: %d\n", info->pag);
	info->off = (unPcb->indiceCodigo[((unPcb->programCounter) * 2)] % tamPag);
	//printf("[crearEstructuraParaMemoria]Voy a leer con offswet: %d\n", info->off);
	info->size = unPcb->indiceCodigo[((unPcb->programCounter) * 2) + 1];
	//printf("[crearEstructuraParaMemoria]Voy a leer el tamano: %d\n", info->size);
	memcpy(informacion, info, 12);
	free(info);
	return;
}

char* leerSentencia(int pagina, int offset, int tamanio, int flag) {
	if ((tamanio + offset) <= 20) {
		posicionMemoria *datos_para_memoria = malloc(sizeof(posicionMemoria));
		datos_para_memoria->off = offset;
		datos_para_memoria->pag = pagina;
		datos_para_memoria->size = tamanio;
		enviarDirecParaLeerMemoria(datos_para_memoria, LEERSENTENCIA);
		sem_wait(&semInstruccion);
		char* sentencia2 = malloc(datos_para_memoria->size);
		memcpy(sentencia2, instruccionLeida, datos_para_memoria->size);
		free(datos_para_memoria);
		if (flag == 0)
			sem_post(&semSentenciaCompleta);
		return sentencia2;
	} else {
		int tamano1 = tamanoPag - offset;
		int tamano2 = tamanio - tamano1;
		char* lectura1 = leerSentencia(pagina, offset, tamano1, 1);
		if (lectura1 == NULL)
			return NULL;
		char* lectura2 = leerSentencia(pagina + 1, 0, tamano2, 1);
		if (lectura2 == NULL)
			return NULL;

		char* nuevo = malloc((20 - offset) + tamanio - (20 - offset));
		memcpy(nuevo, lectura1, (20 - offset));
		memcpy(nuevo + (20 - offset), lectura2, tamanio - (20 - offset));
		free(lectura1);
		free(lectura2);
		sem_post(&semSentenciaCompleta);
		return nuevo;
	}
	char * lecturaMemoria = malloc(12);
	return lecturaMemoria;
}

t_valor_variable obtenerValorCompartida(t_nombre_compartida variable) {
	char * variable_compartida = malloc(strlen(variable) + 1);
	char* barra_cero = "\0";
	memcpy(variable_compartida, variable, strlen(variable));
	memcpy(variable_compartida + (strlen(variable)), barra_cero, 1);
	Serializar(VALORVARIABLECOMPARTIDA, strlen(variable) + 1,
			variable_compartida, cliente);
	sem_wait(&semVariableCompartidaValor);
	printf("[obtenerValorCompartida]compartida en procesar %d",
			valorVaribleCompartida);
	free(variable_compartida);
	return valorVaribleCompartida;
}

t_valor_variable asignarValorCompartida(t_nombre_compartida variable,
		t_valor_variable valor) {
	char *variableCompartida = malloc(5 + strlen(variable));
	char* barra_cero = "\0";
	memcpy(variableCompartida, &valor, 4);
	memcpy(variableCompartida + 4, variable, strlen(variable));
	memcpy(variableCompartida + strlen(variable) + 4, barra_cero, 1);
	printf("[asignarValorCompartida]Variable %s le asigno %d\n",
			variableCompartida + 4, (int*) variableCompartida[0]);
	Serializar(ASIGNOVALORVARIABLECOMPARTIDA, 5 + strlen(variable),
			variableCompartida, cliente);
	free(variableCompartida);
	return valor;
}

void escribir(t_descriptor_archivo descriptorArchivo, void* informacion,
		t_valor_variable tamano) {
	char* envio = malloc(tamano + 4);
	memcpy(envio, informacion, tamano);
	memcpy(envio + tamano, &descriptorArchivo, 4);
	Serializar(IMPRIMIRPROCESO, tamano + 4, envio, cliente);
	free(envio);
}

void wait_kernel(t_nombre_semaforo identificador_semaforo) {
	char* nombre_semaforo = malloc(strlen(identificador_semaforo) + 1);
	char* barra_cero = "\0";
	memcpy(nombre_semaforo, identificador_semaforo,
			strlen(identificador_semaforo));
	memcpy(nombre_semaforo + strlen(identificador_semaforo), barra_cero, 1);
	Serializar(PROCESOWAIT, strlen(nombre_semaforo) + 1, nombre_semaforo,
			cliente);
	sem_wait(&semProcesoBloqueado);
	//devuelve 0 si no se bloquea, 1 si se bloquea
	free(nombre_semaforo);
	return;
}

void signal_kernel(t_nombre_semaforo identificador_semaforo) {
	char* nombre_semaforo = malloc(strlen(identificador_semaforo) + 1);
	char* barra_cero = "\0";
	memcpy(nombre_semaforo, identificador_semaforo,
			strlen(identificador_semaforo));
	memcpy(nombre_semaforo + strlen(identificador_semaforo), barra_cero, 1);
	Serializar(PROCESOSIGNAL, strlen(nombre_semaforo) + 1, nombre_semaforo,
			cliente);
	free(nombre_semaforo);
	return;
}
