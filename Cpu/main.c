#include "main.h"
#define ARCHIVOLOG "/home/utnso/Log/logCpu.txt"
t_log *logger;
archivoConfigCPU* t_archivoConfig;
t_config *config;
struct sockaddr_in direccionKernel;
int32_t cliente;
char* buffer;
char* infoLeida;
int valorVaribleCompartida;
struct sockaddr_in direccionMem;
int32_t codigoAborto;
int32_t clienteMEM;
int32_t bytesRecibidos;
int32_t header;
int32_t paginaHeap, offsetHeap, punteroHeapDeMemoria;
programControlBlock *unPcb;
int32_t tamanoPag;
pthread_t hiloKernel;
pthread_t hiloMemoria;
pthread_t hiloProcesarScript;
pthread_mutex_t mutexProcesar;
t_descriptor_archivo descriptorArchivoAbierto;
sem_t semProcesar;
sem_t semInstruccion;
sem_t semSentenciaCompleta;
sem_t semHayScript;
sem_t semEscribirVariable;
sem_t semDereferenciar;
sem_t semDestruirPCB;
sem_t semVariableCompartidaValor;
sem_t semProcesoBloqueado;
sem_t semProcesoPideHeap;
sem_t semProcesoLiberaHeap;
sem_t semProcesoTerminaLiberaHeap;
sem_t semAbrirArchivo;
sem_t semCerrarArchivo;
sem_t semBorrarArchivo;
sem_t semLeerArchivo;
sem_t semEscribirArchivo;
sem_t semMoverCursor;
int hayScriptEjecutando = 0;
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
				llamarConRetorno, .AnSISOP_llamarSinRetorno = llamarSinRetorno };
AnSISOP_kernel privilegiadas = { .AnSISOP_escribir = escribir, .AnSISOP_wait =
		wait_kernel, .AnSISOP_signal = signal_kernel, .AnSISOP_reservar =
		reservar, .AnSISOP_liberar = liberar, .AnSISOP_abrir = abrir,
		.AnSISOP_borrar = borrar, .AnSISOP_cerrar = cerrar,
		.AnSISOP_leer = leer, .AnSISOP_moverCursor = moverCursor };

int32_t main(int argc, char**argv) {
	Configuracion(argv[1]);
	logger = log_create(ARCHIVOLOG, "CPU", 0, LOG_LEVEL_INFO);
	log_info(logger, "Iniciando CPU\n");
	signal(SIGUSR1, revisarSigusR1);
	signal(SIGINT, revisarSigusR1);
	pthread_create(&hiloKernel, NULL, ConectarConKernel, NULL);
	pthread_create(&hiloMemoria, NULL, conectarConMemoria, NULL);
	pthread_create(&hiloProcesarScript, NULL, procesarScript, NULL);

	pthread_join(hiloKernel, NULL);
	pthread_join(hiloMemoria, NULL);
	pthread_join(hiloProcesarScript, NULL);
	return EXIT_SUCCESS;
}
void revisarSigusR1(int signo) {
	if (signo == SIGUSR1) {
		log_info(logger, "Se recibe SIGUSR1");
		Signal = 1;
	}
	if (signo == SIGINT) {
		log_info(logger, "Se recibe control c");
		if(hayScriptEjecutando == 0){
			Serializar(DESTRUICPUSINPID, 4, &noInteresa, cliente);
			exit(EXIT_FAILURE);
		}
		Signal = 1;
	}
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
	sem_init(&semProcesoPideHeap, 0, 0);
	sem_init(&semProcesoLiberaHeap, 0, 0);
	sem_init(&semProcesoTerminaLiberaHeap, 0, 0);
	sem_init(&semAbrirArchivo, 0, 0);
	sem_init(&semCerrarArchivo, 0, 0);
	sem_init(&semBorrarArchivo, 0, 0);
	sem_init(&semEscribirArchivo, 0, 0);
	sem_init(&semLeerArchivo, 0, 0);
	sem_init(&semMoverCursor, 0, 0);
	Signal = 0;
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
		log_info(logger, "%s", paquete);
		break;
	}
	case MATARPIDPORCONSOLA: {
		log_info(logger, "llego senal para abortar por consola: %d\n",
				unPcb->programId);
		programaAbortado = 1;
		codigoAborto = ABORTOPORCONSOLA;
		break;
	}
	case ABORTOPORCONSOLA: {
		log_info(logger, "llego senal para abortar por consola: %d\n",
				unPcb->programId);
		programaAbortado = 1;
		codigoAborto = ABORTOPORCONSOLA;
		break;
	}
	case ABORTOPORMASRESERVERAQUEPAGINA: {
		log_info(logger,
				"llego senal para abortar por reservar mas memoria que heap: %d\n",
				unPcb->programId);
		sem_post(&semProcesoPideHeap);
		programaAbortado = 1;
		codigoAborto = ABORTOPORMASRESERVERAQUEPAGINA;
		break;
	}
	case ABORTOEXPECIONDEMEMORIA: {
		log_info(logger,
				"llego senal para abortar por excepcion de memoria: %d\n",
				unPcb->programId);
		sem_post(&semProcesoPideHeap);
		programaAbortado = 1;
		codigoAborto = ABORTOEXPECIONDEMEMORIA;
		break;
	}
	case FILESYSTEM: {
		log_info(logger, "Se conecto FS");
		break;
	}
	case KERNEL: {
		log_info(logger, "Se conecto Kernel\n");
		break;
	}
	case CPU: {
		log_info(logger, "Se conecto CPU");
		break;
	}
	case CONSOLA: {
		log_info(logger, "Se conecto Consola");
		break;
	}
	case VALORVARIABLECOMPARTIDA: {
		memcpy(&valorVaribleCompartida, paquete, 4);
		log_info(logger, "compartida en procesar %d", valorVaribleCompartida);
		sem_post(&semVariableCompartidaValor);
		break;
	}
	case MEMORIA: {
		memcpy(&tamanoPag, (paquete), sizeof(int));
		log_info(logger, "Se conecto Memoria\n");
		break;
	}
	case LEERSENTENCIA: {
		log_info(logger, "lei una sentencia de memoria: \n");
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
		log_info(logger,
				"el proceso tiro wait %d y se bloqueo(1 si, 0 no) %d\n",
				unPcb->programId, programaBloqueado);
		sem_post(&semProcesoBloqueado);
		break;
	}
	case PCB: {
		sem_wait(&semDestruirPCB);
		unPcb = deserializarPCB(paquete);
		log_info(logger, "unPcb id: %d\n", unPcb->programId);
		sem_post(&semHayScript);
		break;
	}
	case DATOSKERNELCPU: {
		memcpy(&quantum, paquete, 4);
		memcpy(&quantumSleep, paquete + 4, 4);
		memcpy(&algoritmo, paquete + 8, 4);
		memcpy(&stackSize, paquete + 12, 4);
		log_info(logger, "quatum: %d\n", quantum);
		log_info(logger, "quatum slep: %d\n", quantumSleep);
		log_info(logger, "stack: %d\n", stackSize);
		log_info(logger, "algoritmo: %d\n", algoritmo);
		break;
	}
	case PROCESOPIDEHEAP: {
		memcpy(&paginaHeap, paquete, 4);
		memcpy(&offsetHeap, paquete + 4, 4);
		sem_post(&semProcesoPideHeap);
		break;
	}
	case PROCESOLIBERAHEAP: {
		memcpy(&punteroHeapDeMemoria, paquete, 4);
		sem_post(&semProcesoLiberaHeap);
		break;
	}
	case PROCESOTERMINALIBERAHEAP: {
		sem_post(&semProcesoTerminaLiberaHeap);
		break;
	}
	case ABRIRARCHIVO: {
		int validado;
		memcpy(&validado, paquete, 4);
		if (validado == 0) {
			programaAbortado = 1;
			codigoAborto = ABORTOARCHIVONOEXISTE;
		} else if (validado == 2) {
			programaAbortado = 1;
			codigoAborto = ABORTONOHAYLUGARFS;
		} else {
			memcpy(&descriptorArchivoAbierto, paquete + 4, 4);
		}
		sem_post(&semAbrirArchivo);
		break;
	}
	case CERRARARCHIVO: {
		int validado;
		memcpy(&validado, paquete, 4);
		if (validado == 0) {
			programaAbortado = 1;
			codigoAborto = ABORTOARCHIVONOEXISTE;
		} else {

		}
		sem_post(&semCerrarArchivo);
		break;
	}
	case LEERARCHIVO: {
		int validado;
		memcpy(&validado, paquete, 4);
		if (validado == 0) {
			programaAbortado = 1;
			codigoAborto = ABORTOARCHIVONOEXISTE;
		} else if (validado == 2) {
			programaAbortado = 1;
			codigoAborto = ABORTOLEERSINPERMISOS;
		} else {
			int tamanoLeido;
			memcpy(&tamanoLeido, paquete, 4);
			infoLeida = malloc(tamanoLeido);
			memcpy(infoLeida, paquete, tamanoLeido);
		}
		sem_post(&semLeerArchivo);
		break;
	}
	case ESCRIBIRARCHIVO: {
		int validado;
		memcpy(&validado, paquete, 4);
		if (validado == 0) {
			programaAbortado = 1;
			codigoAborto = ABORTOARCHIVONOEXISTE;
		}
		if (validado == 2) {
			programaAbortado = 1;
			codigoAborto = ABORTOESCRIBIRSINPERMISOS;
		}
		sem_post(&semEscribirArchivo);
		break;
	}
	case MOVERCURSOR: {
		int validado;
		memcpy(&validado, paquete, 4);
		if (validado == 0) {
			programaAbortado = 1;
			codigoAborto = codeArchivoNoexiste;
		}
		sem_post(&semMoverCursor);
		break;
	}
	case BORRARARCHIVO: {
		int validado;
		memcpy(&validado, paquete, 4);
		if (validado == 0) {
			programaAbortado = 1;
			codigoAborto = codeArchivoNoexiste;
		}
		sem_post(&semBorrarArchivo);
		break;
	}
	}

}

bool validarQuantum(int valor) {
	if (algoritmo == 0)
		return true;
	else
		return valor != 0;
}

void procesarScript() {
	while (1) {
		sem_wait(&semHayScript);
		hayScriptEjecutando = 1;
		programaBloqueado = 0;
		programaFinalizado = 0;
		programaAbortado = 0;
		int quantum_aux = quantum;
		int pid = unPcb->programId;
		while (validarQuantum(quantum_aux) && !programaBloqueado
				&& !programaFinalizado && !programaAbortado && !Signal) {
			posicionMemoria* datos_para_memoria = malloc(
					sizeof(posicionMemoria));
			crearEstructuraParaMemoria(unPcb, tamanoPag, datos_para_memoria);
			char* sentencia = leerSentencia(datos_para_memoria->pag,
					datos_para_memoria->off, datos_para_memoria->size, 0);
			sem_wait(&semSentenciaCompleta);
			char* barra_cero = "\0";
			memcpy(sentencia + (datos_para_memoria->size - 1), barra_cero, 1);
			log_info(logger, "[procesarScript]Sentencia: %s de pid %d \n",
					sentencia, pid);
			analizadorLinea(depurarSentencia(sentencia), &primitivas,
					&privilegiadas);
			free(datos_para_memoria);
			free(sentencia);
			unPcb->programCounter++;
			quantum_aux--;
			usleep(quantumSleep * 1000);
		}
		if (programaAbortado) {
			hayScriptEjecutando = 0;
			serializarPCB(unPcb, cliente, codigoAborto);
			destruirPCB(unPcb);
			sem_post(&semDestruirPCB);
		} else if (Signal) {
			hayScriptEjecutando = 0;
			serializarPCB(unPcb, cliente, ABORTODESCONEXIONCPU);
			destruirPCB(unPcb);
			exit(EXIT_FAILURE);
		} else {
			if (programaBloqueado) {
				hayScriptEjecutando = 0;
				serializarPCB(unPcb, cliente, SEBLOQUEOELPROCESO);
				destruirPCB(unPcb);
				sem_post(&semDestruirPCB);
			} else {
				if (algoritmo == 1 && (quantum_aux == 0) && !programaFinalizado
						&& !programaBloqueado && !programaAbortado && !Signal) {
					hayScriptEjecutando = 0;
					serializarPCB(unPcb, cliente, FINDEQUATUM);
					destruirPCB(unPcb);
					sem_post(&semDestruirPCB);
				}
			}
		}

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
	log_info(logger, "[definirVariable]Entre a definir %c\n", nombreVariable);
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
	}

	else if ((nombreVariable >= '0') && (nombreVariable <= '9')) {
		log_info(logger, "[definirVariable]Creando argumento %c\n",
				nombreVariable);
		armarDireccionDeArgumento(direccionVariable);
		list_add(indiceStack->args, direccionVariable);
		log_info(logger,
				"[definirVariable]Direccion de argumento %c es %d %d %d\n",
				nombreVariable, direccionVariable->pag, direccionVariable->off,
				direccionVariable->size);
		indiceStack->tamanoArgs++;
	}

	else if (indiceStack->tamanoVars == 0 && (unPcb->tamanoIndiceStack) > 1) {
		log_info(logger, "[definirVariable]Declarando variable %c de funcion\n",
				nombreVariable);
		armarDireccionDeFuncion(direccionVariable);
		unaVariable->etiqueta = nombreVariable;
		unaVariable->direccion = direccionVariable;
		list_add(indiceStack->vars, unaVariable);
		indiceStack->tamanoVars++;

	}

	else {
		armarProximaDireccion(direccionVariable);
		unaVariable->etiqueta = nombreVariable;
		unaVariable->direccion = direccionVariable;
		list_add(indiceStack->vars, unaVariable);
		indiceStack->tamanoVars++;
	}
	if (programaAbortado == 0) {
		int valor = 0;
		int direccionRetorno = convertirDireccionAPuntero(direccionVariable);
		log_info(logger, "[definirVariable]Defino %c ubicada en %d\n",
				nombreVariable, direccionRetorno);
		enviarDirecParaEscribirMemoria(direccionVariable, valor);
		return (direccionRetorno);
	} else
		return 0;

}

void armarDireccionDeFuncion(posicionMemoria *direccionReal) {
	indiceDeStack *stackActual = (indiceDeStack*) list_get(unPcb->indiceStack,
			unPcb->tamanoIndiceStack - 1);
	int existenVariables = 0;
	int posMax = unPcb->tamanoIndiceStack - 2;
	while (posMax >= 0) {
		if (((indiceDeStack*) list_get(unPcb->indiceStack, posMax))->tamanoVars
				!= 0) {
			existenVariables = 1;
		}
		posMax--;
	}
	if (stackActual->tamanoArgs == 0 && stackActual->tamanoVars == 0) {
		printf(
				"[armarDireccionDeFuncion]Entrando a definir variable en contexto sin argumentos y sin vars\n");
		if (!existenVariables) {
			armarDireccionPrimeraPagina(direccionReal);
		} else {
			int posicionStackAnterior = unPcb->tamanoIndiceStack - 2;
			int posicionUltimaVariable =
					((indiceDeStack*) (list_get(unPcb->indiceStack,
							unPcb->tamanoIndiceStack - 2)))->tamanoVars - 1;
			log_info(logger,
					"[armarDireccionDeFuncion]Entrando a definir variable en contexto sin argumentos y sin vars en stack:%d var:%d\n",
					posicionStackAnterior, posicionUltimaVariable);
			proximaDireccion(posicionStackAnterior, posicionUltimaVariable,
					direccionReal);
		}
	} else if (stackActual->tamanoVars == 0) {
		log_info(logger,
				"[armarDireccionDeFuncion]Entrando a definir variable a partir del ultimo argumento\n");
		int posicionStackActual = unPcb->tamanoIndiceStack - 1;
		int posicionUltimoArgumento = (stackActual)->tamanoArgs - 1;
		proximaDireccionArg(posicionStackActual, posicionUltimoArgumento,
				direccionReal);
	} else {
		log_info(logger,
				"[armarDireccionDeFuncion]Entrando a definir variable a partir de la ultima variable\n");
		int posicionStackActual = unPcb->tamanoIndiceStack - 1;
		int posicionUltimaVariable = stackActual->tamanoVars - 1;
		proximaDireccion(posicionStackActual, posicionUltimaVariable,
				direccionReal);
	}
	return;
}

void proximaDireccionArg(int posStack, int posUltVar,
		posicionMemoria* direccionReal) {
	posicionMemoria *direccion = malloc(sizeof(posicionMemoria));
	log_info(logger, "Entre a proximadirecArg\n");
	int offset = ((posicionMemoria*) (list_get(
			((indiceDeStack*) (list_get(unPcb->indiceStack, posStack)))->args,
			posUltVar)))->off + 4;
	log_info(logger, "Offset siguiente es %d\n", offset);
	if (offset >= tamanoPag) {
		direccion->pag =
				((posicionMemoria*) (list_get(
						((indiceDeStack*) (list_get(unPcb->indiceStack,
								posStack)))->args, posUltVar)))->pag + 1;
		if (direccion->pag > unPcb->cantidadDePaginas + stackSize) {
			//TODO: ABORTAR PROCESO POR STACKOVERFLOW
			programaAbortado = 1;
			codigoAborto = ABORTOSTACKOVERFLOW;
			return;
		}

		direccion->off = 0;
		direccion->size = 4;
		memcpy(direccionReal, direccion, sizeof(posicionMemoria));
		free(direccion);
	} else {
		direccion->pag =
				((posicionMemoria*) (list_get(
						((indiceDeStack*) (list_get(unPcb->indiceStack,
								posStack)))->args, posUltVar)))->pag;
		direccion->off = offset;
		direccion->size = 4;
		memcpy(direccionReal, direccion, sizeof(posicionMemoria));
		free(direccion);
	}
	return;
}

t_puntero obtenerPosicionVariable(t_nombre_variable nombreVariable) {
	log_info(logger, "[obtenerPosicionVariable]Obtener posicion de %c\n",
			nombreVariable);
	int posicionStack = unPcb->tamanoIndiceStack - 1;
	int direccionRetorno;
	if ((nombreVariable >= '0') && (nombreVariable <= '9')) {
		posicionMemoria *direccion =
				(posicionMemoria*) (list_get(
						((indiceDeStack*) (list_get(unPcb->indiceStack,
								posicionStack)))->args,
						(int) nombreVariable - 48));
		direccionRetorno = convertirDireccionAPuntero(direccion);
		printf(
				"[obtenerPosicionVariable]Obtengo valor de %c: %d %d (tamaño: %d)\n",
				nombreVariable, direccion->pag, direccion->off,
				direccion->size);
		return (direccionRetorno);
	} else {
		variable *variableNueva;
		int posMax = (((indiceDeStack*) (list_get(unPcb->indiceStack,
				posicionStack)))->tamanoVars) - 1;
		while (posMax >= 0) {
			variableNueva = ((variable*) (list_get(
					((indiceDeStack*) (list_get(unPcb->indiceStack,
							posicionStack)))->vars, posMax)));
			log_info(logger, "[obtenerPosicionVariable]Variable: %c\n",
					variableNueva->etiqueta);
			if (variableNueva->etiqueta == nombreVariable) {
				direccionRetorno =
						convertirDireccionAPuntero(
								((variable*) (list_get(
										((indiceDeStack*) (list_get(
												unPcb->indiceStack,
												posicionStack)))->vars, posMax)))->direccion);
				printf(
						"[obtenerPosicionVariable]Obtengo valor de %c: %d %d (tamaño: %d)\n",
						variableNueva->etiqueta, variableNueva->direccion->pag,
						variableNueva->direccion->off,
						variableNueva->direccion->size);
				return (direccionRetorno);
			}
			posMax--;
		}
	}
	log_info(logger, "[ERROR]No debería llegar aca\n");
}

void destruirContextoActual(void) {
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
	log_info(logger, "[destruirContextoActual]Contexto destruido\n");

}

void finalizar(void) {
	log_info(logger, "[finalizar]Finalizar funcion\n");
	destruirContextoActual();
	if (unPcb->tamanoIndiceStack == 0) {

		log_info(logger, "[finalizar]Programa Finalizado\n");
		programaFinalizado = 1;
		Serializar(PROGRAMATERMINADO, 4, &noInteresa, cliente);
		Serializar(PORLASDUDAS, 4, &noInteresa, cliente);
		Serializar(PORLASDUDAS, 4, &noInteresa, cliente);
		Serializar(PORLASDUDAS, 4, &noInteresa, cliente);
		destruirPCB(unPcb);
		hayScriptEjecutando = 0;
		sem_post(&semDestruirPCB);
	}
}

indiceDeStack* crearStack() {
	indiceDeStack *stack = malloc(sizeof(indiceDeStack));
	int posicionStack = unPcb->tamanoIndiceStack;
	stack->pos = posicionStack;
	stack->args = list_create();
	stack->vars = list_create();
	stack->tamanoArgs = 0;
	stack->tamanoVars = 0;
	memcpy(&stack->retPos, &unPcb->programCounter, 4);
	return stack;
}
void llamarSinRetorno(t_nombre_etiqueta etiqueta) {
	indiceDeStack *nuevoContexto = crearStack(nuevoContexto);
	printf(
			"[llamarSinRetorno]Creo nuevo contexto con pos: %d que debe volver en la sentencia %d\n",
			nuevoContexto->pos, nuevoContexto->retPos);
	list_add(unPcb->indiceStack, nuevoContexto);
	unPcb->tamanoIndiceStack++;
	irAlLabel(etiqueta);
}

void llamarConRetorno(t_nombre_etiqueta etiqueta, t_puntero punteroRetorno) {
	posicionMemoria *direccionRetorno = malloc(sizeof(posicionMemoria));
	convertirPunteroADireccion(punteroRetorno, direccionRetorno);
	indiceDeStack *nuevoContexto = crearStack(nuevoContexto);
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
	char** string_cortado = string_split(etiqueta, "\n");
	log_info(logger, "[irAlLabel]Busco etiqueta: %s y mide: %d\n", etiqueta,
			strlen(etiqueta));
	instruccion = metadata_buscar_etiqueta(string_cortado[0],
			unPcb->indiceEtiquetas, unPcb->tamanoindiceEtiquetas);
	log_info(logger, "[irAlLabel]Ir a instruccion %d\n", instruccion);
	unPcb->programCounter = instruccion - 1;
	return;
}

void retornar(t_valor_variable valorRetorno) {

	indiceDeStack *contextoAFinalizar = list_get(unPcb->indiceStack,
			unPcb->tamanoIndiceStack - 1);
	int direccionRetorno = convertirDireccionAPuntero(
			&(contextoAFinalizar->retVar));
	asignar(direccionRetorno, valorRetorno);
	log_info(logger, "[retornar]Retornando %d en %d\n", valorRetorno,
			direccionRetorno);

	unPcb->programCounter = contextoAFinalizar->retPos;
	destruirContextoActual();
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
	log_info(logger, "[dereferenciar]Dereferenciar %d y su valor es: %d\n",
			puntero, valorDerenferenciado);
	return valorDerenferenciado;
}

void asignar(t_puntero punteroAVariable, t_valor_variable valor) {
	log_info(logger, "[asignar]Asignando en %d el valor %d\n", punteroAVariable,
			valor);
	posicionMemoria *direccion = malloc(sizeof(posicionMemoria));
	convertirPunteroADireccion(punteroAVariable, direccion);
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

void armarDireccionDeArgumento(posicionMemoria *direccionReal) {

	if (((indiceDeStack*) list_get(unPcb->indiceStack,
			unPcb->tamanoIndiceStack - 1))->tamanoArgs == 0) {
		log_info(logger, "[armarDireccionDeArgumento]No hay argumentos\n");
		int posicionStackAnterior = unPcb->tamanoIndiceStack - 2;
		int posicionUltimaVariable = ((indiceDeStack*) (list_get(
				unPcb->indiceStack, unPcb->tamanoIndiceStack - 2)))->tamanoVars
				- 1;
		proximaDireccion(posicionStackAnterior, posicionUltimaVariable,
				direccionReal);
	} else {
		log_info(logger, "[armarDireccionDeArgumento]Busco ultimo argumento\n");
		int posicionStackActual = unPcb->tamanoIndiceStack - 1;
		int posicionUltimoArgumento = ((indiceDeStack*) (list_get(
				unPcb->indiceStack, unPcb->tamanoIndiceStack - 1)))->tamanoArgs
				- 1;
		proximaDireccion(posicionStackActual, posicionUltimoArgumento,
				direccionReal);
	}
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
		if (direccion->pag > unPcb->cantidadDePaginas + stackSize) {
			//TODO: ABORTAR PROCESO POR STACKOVERFLOW
			programaAbortado = 1;
			codigoAborto = ABORTOSTACKOVERFLOW;
			return;
		}
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
			"[enviarDirecParaEscribirMemoria]Quiero escribir en la direccion %d %d (tamaño: %d) el valor: %d\n",
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
			"[enviarDirecParaLeerMemoria]Quiero leer en la direccion: %d %d (tamaño: %d)\n",
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
	//log_info(logger,"[crearEstructuraParaMemoria]Voy a leer la pagina: %d\n", info->pag);
	info->off = (unPcb->indiceCodigo[((unPcb->programCounter) * 2)] % tamPag);
	//log_info(logger,"[crearEstructuraParaMemoria]Voy a leer con offswet: %d\n", info->off);
	info->size = unPcb->indiceCodigo[((unPcb->programCounter) * 2) + 1];
	//log_info(logger,"[crearEstructuraParaMemoria]Voy a leer el tamano: %d\n", info->size);
	memcpy(informacion, info, 12);
	free(info);
	return;
}

char* leerSentencia(int pagina, int offset, int tamanio, int flag) {
	if ((tamanio + offset) <= tamanoPag) {
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

		char* nuevo = malloc(
				(tamanoPag - offset) + tamanio - (tamanoPag - offset));
		memcpy(nuevo, lectura1, (tamanoPag - offset));
		memcpy(nuevo + (tamanoPag - offset), lectura2,
				tamanio - (tamanoPag - offset));
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
	log_info(logger, "[obtenerValorCompartida]compartida en procesar %d",
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
	log_info(logger, "[asignarValorCompartida]Variable %s le asigno %d\n",
			variableCompartida + 4, (int*) variableCompartida[0]);
	Serializar(ASIGNOVALORVARIABLECOMPARTIDA, 5 + strlen(variable),
			variableCompartida, cliente);
	free(variableCompartida);
	return valor;
}

void escribir(t_descriptor_archivo descriptorArchivo, void* informacion,
		t_valor_variable tamano) {
	void* envio = malloc(tamano + 4);
	memcpy(envio, informacion, tamano);
	memcpy(envio + tamano, &descriptorArchivo, 4);
	if (descriptorArchivo == 0 || descriptorArchivo == 1
			|| descriptorArchivo == 2) {
		Serializar(IMPRIMIRPROCESO, tamano + 4, envio, cliente);
		free(envio);

		return;
	}
	Serializar(ESCRIBIRARCHIVO, tamano + 4, envio, cliente);
	sem_wait(&semEscribirArchivo);
	free(envio);
	return;
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

t_puntero reservar(t_valor_variable espacio) {
	int resultadoEjecucion;
	int pid = unPcb->programId;
	void* envio = malloc(8);
	memcpy(envio, &pid, sizeof(int));
	memcpy(envio + 4, &espacio, sizeof(int));
	Serializar(PROCESOPIDEHEAP, 8, envio, cliente);
	sem_wait(&semProcesoPideHeap);
	if (programaAbortado == 0) {
		t_puntero puntero = paginaHeap * tamanoPag + offsetHeap;
		log_info(logger, "El puntero es %d", puntero);
		free(envio);
		return puntero;
	}
	return 0;
}
void liberar(t_puntero puntero) {
	posicionMemoria *datos_para_memoria = malloc(sizeof(posicionMemoria));
	int num_paginaDelStack = puntero / tamanoPag;
	int offsetDelStack = puntero - (num_paginaDelStack * tamanoPag);
	//datos_para_memoria->off = offsetDelStack;
	//datos_para_memoria->pag = num_paginaDelStack;
	//datos_para_memoria->size = 4;
	//enviarDirecParaLeerMemoria(datos_para_memoria,PROCESOLIBERAHEAP);
	//sem_wait(&semProcesoLiberaHeap);
	int pid = unPcb->programId;
	int tamanio = sizeof(t_puntero);

	//int num_paginaHeap = (punteroHeapDeMemoria) / tamanoPag + unPcb->cantidadDePaginas + stackSize;
	//int offsetHeap = punteroHeapDeMemoria - (num_paginaHeap * tamanoPag);
	int offsetHeap = offsetDelStack - 8;
	void* envio = malloc(8 + tamanio);
	memcpy(envio, &pid, sizeof(int));
	memcpy(envio + 4, &num_paginaDelStack, sizeof(int));
	memcpy(envio + 8, &offsetHeap, tamanio);
	Serializar(PROCESOLIBERAHEAP, 8 + tamanio, envio, cliente);
	sem_wait(&semProcesoTerminaLiberaHeap);
	free(envio);
	free(datos_para_memoria);

}

t_descriptor_archivo abrir(t_direccion_archivo direccion, t_banderas flags) {

	int descriptor;
	char *flagsAConcatenar = string_new();
	if (flags.creacion == true) {
		log_info(logger, "Tiene permiso de creacion\n");
		string_append(&flagsAConcatenar, "c");
	}
	if (flags.lectura == true) {
		log_info(logger, "Tiene permiso de lectura\n");
		string_append(&flagsAConcatenar, "r");
	}
	if (flags.escritura == true) {
		log_info(logger, "Tiene permiso de escritura\n");
		string_append(&flagsAConcatenar, "w");
	}
	int tamanoFlags = sizeof(char) * strlen(flagsAConcatenar);
	int tamanoDireccion = sizeof(char) * strlen(direccion);
	void *envio = malloc(8 + tamanoFlags + tamanoDireccion);
	memcpy(envio, &tamanoDireccion, 4);
	memcpy(envio + 4, &tamanoFlags, 4);
	memcpy(envio + 8, flagsAConcatenar, tamanoFlags);
	memcpy(envio + 8 + tamanoFlags, direccion, tamanoDireccion);
	Serializar(ABRIRARCHIVO, 8 + tamanoFlags + tamanoDireccion, envio, cliente);
	sem_wait(&semAbrirArchivo);
	free(envio);
	if (programaAbortado == 0)
		return descriptorArchivoAbierto;
}
void borrar(t_descriptor_archivo descriptor) {
	void * envio = malloc(4);
	memcpy(envio, &descriptor, 4);
	Serializar(BORRARARCHIVO, 4, envio, cliente);
	sem_wait(&semBorrarArchivo);
	free(envio);
}
void cerrar(t_descriptor_archivo descriptor) {
	void * envio = malloc(4);
	memcpy(envio, &descriptor, 4);
	Serializar(CERRARARCHIVO, 4, envio, cliente);
	sem_wait(&semCerrarArchivo);
	free(envio);
}

void leer(t_descriptor_archivo descriptor, t_puntero puntero,
		t_valor_variable tamano) {
	void * envio = malloc(8);
	memcpy(envio, &descriptor, 4);
	memcpy(envio + 4, &tamano, 4);
	Serializar(LEERARCHIVO, 8, envio, cliente);
	sem_wait(&semLeerArchivo);
	if (programaAbortado == 0) {
		char *infoLeidaChar = string_new();
		string_append(&infoLeidaChar, infoLeida);
		log_info(logger, "info leida %s", infoLeidaChar);
		int pagina = puntero / tamanoPag;
		int offset = puntero - (tamanoPag * pagina);
		void* envioPagina = malloc(tamanoPag + 4 * sizeof(int));
		memcpy(envioPagina, &unPcb->programId, 4);
		memcpy(envioPagina + 4, &pagina, sizeof(int));
		memcpy(envioPagina + 8, &tamano, sizeof(int));
		memcpy(envioPagina + 12, &offset, sizeof(int));
		memcpy(envioPagina + 16, infoLeida, tamano);
		Serializar(PAGINA, tamanoPag + 4 * sizeof(int), envioPagina,
				clienteMEM);
		free(infoLeida);
	}

	free(envio);
}

void moverCursor(t_descriptor_archivo descriptor, t_valor_variable posicion) {
	void * envio = malloc(8);
	memcpy(envio, &descriptor, 4);
	memcpy(envio + 4, &posicion, 4);
	Serializar(MOVERCURSOR, 8, envio, cliente);
	sem_wait(&semMoverCursor);
	free(envio);
}

