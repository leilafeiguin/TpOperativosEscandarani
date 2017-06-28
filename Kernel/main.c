#include "main.h"

struct sockaddr_in direccionCliente;
uint32_t tamanoDireccion;
script* unScript;
int32_t fdmax;
int32_t cpuDisponible;
int32_t newfd;        // descriptor de socket de nueva conexión aceptada
int32_t header;    // buffer para datos del cliente
int32_t nbytes;
int32_t i, j;
fd_set master;   // conjunto maestro de descriptores de fichero
fd_set read_fds; // conjunto temporal de descriptores de fichero para select()
archivoConfigKernel* t_archivoConfig;
t_config *config;
struct sockaddr_in direccionMem;
int32_t clienteMEM;
char* buffer;
char* punteroPaginaHeap;
char* nuevaPaginaHeap;
char buf[3];    // buffer para datos del cliente
int32_t servidor;
int32_t MARCOS_SIZE;
int32_t activado;
int32_t clientefs;
int32_t bytesRecibidos;
int32_t tamanoPaquete;
int32_t processID = 0;
struct sockaddr_in direccionFs;
struct sockaddr_in direccionServidor;
t_queue* colaNew;
t_queue* colaCodigosAMemoria;
t_queue* colaReady;
t_queue* colaExec;
t_queue* colaExit;
t_queue* colaCpu;
t_queue* colaProcesosConsola;
t_queue* colaProcesosBloqueados;
t_queue** colas_semaforos;
sem_t gradoMultiprogramacion;
sem_t semUnScript;
sem_t semNew;
sem_t semReady;
sem_t semEnvioPaginas;
sem_t semProcesar;
sem_t semCpu;
sem_t semEntraElProceso;
sem_t semPaginaEnviada;
sem_t semPunteroPaginaHeap;
pthread_mutex_t mutexColaNew;
pthread_mutex_t mutexColaExit;
pthread_mutex_t mutexColaEx;
pthread_mutex_t mutexColaReady;
pthread_mutex_t mutexProcesar;
pthread_mutex_t mutexProcesarPaquetes;
pthread_mutex_t mutexColaCpu;
pthread_mutex_t mutexProcesarScript;
pthread_mutex_t mutexConfig;
pthread_mutex_t mutexProcesosBloqueados;
pthread_t hiloPlanificadorLargoPlazo;
pthread_t hiloEnviarProceso;
pthread_t hiloPlanificadorCortoPlazo;
pthread_t hiloConexionFS;
pthread_t hiloConexionMemoria;
int noInteresa;
t_log * logger;
datosHeap tablaHeap[100];
int indiceLibreHeap = 0;

int32_t main(int argc, char**argv) {
	logger = log_create("KERNEL.log", "KERNEL", 0, LOG_LEVEL_INFO);

	configuracion(argv[1]);
	conectarConMemoria();
	ConectarConFS();
	pthread_create(&hiloPlanificadorLargoPlazo, NULL, planificadorLargoPlazo,
	NULL);
	pthread_create(&hiloPlanificadorCortoPlazo, NULL, planificadorCortoPlazo,
	NULL);

	pthread_create(&hiloEnviarProceso, NULL, procesarScript, NULL);
	levantarServidor();
	pthread_join(hiloPlanificadorLargoPlazo, NULL);
	pthread_join(hiloPlanificadorCortoPlazo, NULL);
	pthread_join(hiloEnviarProceso, NULL);
	return EXIT_SUCCESS;
}

void configuracion(char*dir) {
	t_archivoConfig = malloc(sizeof(archivoConfigKernel));
	configuracionKernel(t_archivoConfig, config, dir);
	sem_init(&semUnScript, 0, 0);
	sem_init(&semNew, 0, 0);
	sem_init(&semReady, 0, 0);
	sem_init(&semEntraElProceso, 0, 0);
	sem_init(&semPaginaEnviada, 0, 0);
	sem_init(&semEnvioPaginas, 0, 1);
	sem_init(&semCpu, 0, 0);
	sem_init(&semPunteroPaginaHeap, 0, 0);
	sem_init(&semProcesar, 0, 1);
	//ojo con este semaforo
	sem_init(&gradoMultiprogramacion, 0, t_archivoConfig->GRADO_MULTIPROG);
	colaNew = queue_create();
	colaExec = queue_create();
	colaReady = queue_create();
	colaExit = queue_create();
	colaCpu = queue_create();
	colaProcesosConsola = queue_create();
	colaCodigosAMemoria = queue_create();
	colas_semaforos = malloc(
			strlen((char*) t_archivoConfig->SEM_INIT) * sizeof(char*));

	for (i = 0; i < strlen((char*) t_archivoConfig->SEM_INIT) / sizeof(char*);
			i++) {

		colas_semaforos[i] = malloc(sizeof(t_queue*));
		colas_semaforos[i] = queue_create();
	}
}
int32_t conectarConMemoria() {
	llenarSocketAdrrConIp(&direccionMem, t_archivoConfig->IP_MEMORIA,
			t_archivoConfig->PUERTO_MEMORIA);
	clienteMEM = socket(AF_INET, SOCK_STREAM, 0);
	if (connect(clienteMEM, (void*) &direccionMem, sizeof(direccionMem)) != 0) {
		perror("No se pudo conectar con memoria\n");
		return 1;
	}
	Serializar(KERNEL, 4, &noInteresa, clienteMEM);
	paquete* paqueteRecibido = Deserializar(clienteMEM);
	if (paqueteRecibido->header < 0) {
		perror("Memoria se desconectó");
		return 1;
	}
	procesar(paqueteRecibido->package, paqueteRecibido->header,
			paqueteRecibido->size);

	return 0;
}
int32_t ConectarConFS() {
	llenarSocketAdrrConIp(&direccionFs, t_archivoConfig->IP_FS,
			t_archivoConfig->PUERTO_FS);
	clientefs = socket(AF_INET, SOCK_STREAM, 0);
	if (connect(clientefs, (void*) &direccionFs, sizeof(direccionFs)) != 0) {
		perror("No se pudo conectar con fs\n");
		return 1;
	}
	Serializar(KERNEL, 4, &noInteresa, clientefs);
	paquete* paqueteRecibido = Deserializar(clientefs);
	if (paqueteRecibido->header < 0) {
		perror("Kernel se desconectó");
		return 1;
	}
	procesar(paqueteRecibido->package, paqueteRecibido->header,
			paqueteRecibido->size);
	return 0;
}
int32_t levantarServidor() {
	FD_ZERO(&master);    // borra los conjuntos maestro y temporal
	FD_ZERO(&read_fds);
	llenarSocketAdrr(&direccionServidor, 5000);
	servidor = socket(AF_INET, SOCK_STREAM, 0);
	activado = 1;
	setsockopt(servidor, SOL_SOCKET, SO_REUSEADDR, &activado, sizeof(activado));
	if (bind(servidor, (void*) &direccionServidor, sizeof(direccionServidor))
			!= 0) {
		perror("Falló el bind\n");
		return 1;
	}
	printf("Estoy escuchando\n");
	listen(servidor, 100);
	FD_SET(0, &master);
	FD_SET(clienteMEM, &master);
	FD_SET(clientefs, &master);
	FD_SET(servidor, &master);
	// seguir la pista del descriptor de fichero mayor
	fdmax = servidor; // por ahora es éste
	while (1) {
		read_fds = master; // cópialo
		if (select(fdmax + 1, &read_fds, NULL, NULL, NULL) == -1) {
			perror("select");
			exit(1);
		}

		for (i = 0; i <= fdmax; i++) {
			if (FD_ISSET(i, &read_fds)) { // ¡¡tenemos datos!!
				if (i == servidor) {

					// gestionar nuevas conexiones
					tamanoDireccion = sizeof(direccionCliente);
					if ((newfd = accept(servidor, (void*) &direccionCliente,
							&tamanoDireccion)) == -1) {
						perror("accept\n");
					} else {
						printf("numer serv%d\n", newfd);
						FD_SET(newfd, &master); // añadir al conjunto maestro
						if (newfd > fdmax) {    // actualizar el máximo
							fdmax = newfd;
						}
						printf("selectserver: new connection from %s on "
								"socket %d\n",
								inet_ntoa(direccionCliente.sin_addr), newfd);
					}
				} else {
					if (i == 0) {
						char * codigoKernel = malloc(4);
						int logitudIO = read(0, codigoKernel, 4);
						if (logitudIO > 0) {
							int codigoOperacion = (int) (*codigoKernel) - 48;
							printf("Got data on stdin: %d\n", codigoOperacion);
							procesarEntrada(codigoOperacion);
							free(codigoKernel);
						} else {
							// fd closed
							perror("read()");
						}
					} else {
						// gestionar datos de un cliente
						paquete* paqueteRecibido = Deserializar(i);

						// error o conexión cerrada por el cliente
						if (paqueteRecibido->header == -1) {
							// conexión cerrada
							//PROCESAR: SI ES CONSOLA, ABORTAR PIDS
							//SI ES CPU, SCAARLA DE LA LISA
							close(i); // bye!
							FD_CLR(i, &master); // eliminar del conjunto maestro
							printf("selectserver: socket %d hung up\n", i);
						} else if (paqueteRecibido->header == -2) {
							close(i); // bye!
							FD_CLR(i, &master); // eliminar del conjunto maestro
							perror("recv");
						} else {

							procesar(paqueteRecibido->package,
									paqueteRecibido->header,
									paqueteRecibido->size, i);

						}
					}
				}
			}
		}
	}
}

void procesarEntrada(int codigoOperacion) {
	switch (codigoOperacion) {
	case 1: {

		break;
	}
	case 2: {
		int pidAMatar;
		printf("Ingrese pid a finalizar\n");
		scanf("%d", &pidAMatar);
		//TODO avisar a consola
		abortarProgramaPorConsola(pidAMatar, -35);
		//TODO cambiar codigo
		break;
	}
	case 3: {

		break;
	}
	case 4: {

		break;
	}
	}
}

void procesarScript() {
	while (1) {
		sem_wait(&semUnScript);
		pthread_mutex_lock(&mutexProcesarScript);
		int cantidadDePaginas = ceil(
				(double) unScript->tamano / (double) MARCOS_SIZE);
		int cantidadDePaginasToales = cantidadDePaginas
				+ t_archivoConfig->STACK_SIZE;
		Serializar(TAMANO, sizeof(int), &cantidadDePaginasToales, clienteMEM);
		sem_wait(&semEntraElProceso);
		if (header == OK) {
			proceso* unProceso = malloc(sizeof(proceso));
			programControlBlock* unPcb = malloc(sizeof(programControlBlock));
			procesoConsola* unaConsola = malloc(sizeof(procesoConsola));

			unPcb->cantidadDePaginas = cantidadDePaginas;
			crearPCB(unScript->codigo, unPcb);
			unaConsola->pid = processID;
			unaConsola->consola = unScript->socket;
			//TODO semaforo?
			queue_push(colaProcesosConsola, unaConsola);
			unProceso->pcb = unPcb;
			unProceso->socketCONSOLA = unScript->socket;
			char * enviocantidadDePaginas = malloc(2 * sizeof(int));
			memcpy(enviocantidadDePaginas, &processID, sizeof(int));
			memcpy(enviocantidadDePaginas + 4, &cantidadDePaginasToales,
					sizeof(int));
			Serializar(INICIALIZARPROCESO, 8, enviocantidadDePaginas,
					clienteMEM);
			Serializar(PID, 4, &processID, unScript->socket);
			enviarProcesoAMemoria(unPcb->cantidadDePaginas, unScript->codigo,
					unScript->tamano);
			sem_wait(&semEnvioPaginas);
			pthread_mutex_lock(&mutexColaNew);
			queue_push(colaNew, unProceso);
			pthread_mutex_unlock(&mutexColaNew);
			sem_post(&semNew);
			free(unScript->codigo);
		}
		pthread_mutex_unlock(&mutexProcesarScript);
	}

}

void procesar(char * paquete, int32_t id, int32_t tamanoPaquete, int32_t socket) {
	switch (id) {
	case ARCHIVO: {
		//printf("%s\n", paquete);
		//printf("%d\n", tamanoPaquete);
		//paquete[tamanoPaquete] = '\0';
		unScript = malloc(sizeof(script));
		unScript->tamano = tamanoPaquete;
		unScript->codigo = malloc(tamanoPaquete);
		unScript->socket = socket;
		memcpy(unScript->codigo, paquete, tamanoPaquete);
		sem_post(&semUnScript);
		break;
	}
	case FILESYSTEM: {
		printf("Se conecto FS\n");
		break;
	}

	case CPU: {
		datosKernelACpu enviar;
		enviar.quantum = t_archivoConfig->QUANTUM;
		enviar.quantumSleep = t_archivoConfig->QUANTUM_SLEEP;
		enviar.stack = t_archivoConfig->STACK_SIZE;
		printf("%s\n", t_archivoConfig->ALGORITMO);
		if (!(strcmp(t_archivoConfig->ALGORITMO, "RR"))) {
			enviar.algoritmo = 1;
		} else {
			enviar.algoritmo = 0;
		}
		Serializar(DATOSKERNELCPU, sizeof(datosKernelACpu), &enviar, socket);
		pthread_mutex_lock(&mutexColaCpu);
		queue_push(colaCpu, socket);
		pthread_mutex_unlock(&mutexColaCpu);
		sem_post(&semCpu);
		printf("Se conecto CPU\n");
		break;
	}
	case CONSOLA: {
		printf("Se conecto Consola\n");
		break;
	}
	case MEMORIA: {
		memcpy(&MARCOS_SIZE, (paquete), sizeof(int));
		printf("Se conecto memoria\n");
		break;
	}
	case CODIGO: {
		break;
	}
	case OK: {
		break;
	}
	case PAGINAENVIADA: {
		sem_post(&semPaginaEnviada);
		break;
	}
	case ENTRAPROCESO: {
		sem_post(&semEntraElProceso);
		memcpy(&header, paquete, 4);

		printf("header %d\n", header);
		break;
	}
	case PROGRAMATERMINADO: {
		pthread_mutex_lock(&mutexColaEx);
		proceso * procesoTerminado = sacarProcesoDeEjecucion(socket);
		pthread_mutex_unlock(&mutexColaEx);
		pthread_mutex_lock(&mutexColaExit);
		queue_push(colaExit, procesoTerminado);
		pthread_mutex_unlock(&mutexColaExit);
		pthread_mutex_lock(&mutexColaCpu);
		queue_push(colaCpu, socket);
		pthread_mutex_unlock(&mutexColaCpu);
		//TODO liberar paginas procesos memoria
		Serializar(PROGRAMATERMINADO, 4, &procesoTerminado->pcb->programId,
				procesoTerminado->socketCONSOLA);
		sem_post(&semCpu);
		break;
	}
	case PUNTEROPAGINAHEAP:
	case ESCRITURAPAGINA: {
		punteroPaginaHeap = malloc(tamanoPaquete);
		memcpy(punteroPaginaHeap, paquete, tamanoPaquete);
		sem_post(&semPunteroPaginaHeap);
		break;
	}
	case MATARPIDPORCONSOLA: {
		int pid;
		char* fechaFIn = temporal_get_string_time();
		memcpy(&pid, paquete, sizeof(int));
		abortarProgramaPorConsola(pid, codeFinalizarPrograma);
		break;
	}
	case NOENTROPROCESO: {
		//TODO abortar
		break;
	}
		//procesar pid muerto
		//semaforear los procesar
	case DESCONECTARCONSOLA: {
		abortarTodosLosProgramasDeConsola(socket);
		break;
	}
	case FINDEQUATUM: {
		programControlBlock* pcbRecibido = deserializarPCB(paquete);
		proceso* unProceso = sacarProcesoDeEjecucionPorPid(
				pcbRecibido->programId);
		destruirPCB(unProceso->pcb);
		unProceso->pcb = pcbRecibido;
		pthread_mutex_lock(&mutexColaReady);
		queue_push(colaReady, unProceso);
		pthread_mutex_unlock(&mutexColaReady);
		sem_post(&semReady);
		pthread_mutex_lock(&mutexColaCpu);
		queue_push(colaCpu, socket);
		pthread_mutex_unlock(&mutexColaCpu);
		sem_post(&semCpu);
		break;
	}
	case ASIGNOVALORVARIABLECOMPARTIDA: {
		proceso* unProceso;
		int valor;
		memcpy(&valor, paquete, 4);
		char * variable = malloc(tamanoPaquete - 4);
		memcpy(variable, paquete + 4, tamanoPaquete);
		pthread_mutex_lock(&mutexColaEx);
		unProceso = sacarProcesoDeEjecucion(socket);
		queue_push(colaExec, unProceso);
		pthread_mutex_unlock(&mutexColaEx);
		pthread_mutex_lock(&mutexConfig);
		escribeVariable(variable, valor);
		pthread_mutex_unlock(&mutexConfig);
		break;
	}

	case VALORVARIABLECOMPARTIDA: {
		proceso* unProceso;
		char * variable = malloc(tamanoPaquete);
		memcpy(variable, paquete, tamanoPaquete);
		pthread_mutex_lock(&mutexColaEx);
		unProceso = sacarProcesoDeEjecucion(socket);
		queue_push(colaExec, unProceso);
		pthread_mutex_unlock(&mutexColaEx);
		pthread_mutex_lock(&mutexConfig);
		int envio = pideVariable(variable);
		Serializar(VALORVARIABLECOMPARTIDA, sizeof(int), &envio, socket);
		pthread_mutex_unlock(&mutexConfig);
		free(variable);
		break;
	}

	case IMPRIMIRPROCESO: {		//imprimir
		proceso* unProceso;
		int tamanioAEnviar;
		int descriptor;
		//TODO descriptor para escribir archivos
		int tamanioTexto = tamanoPaquete - 4;
		char *impresion = malloc(tamanioTexto + 4);
		memcpy(impresion, paquete, tamanioTexto);
		memcpy(&descriptor, paquete + tamanioTexto, 4);
		pthread_mutex_lock(&mutexColaEx);
		unProceso = sacarProcesoDeEjecucion(socket);
		queue_push(colaExec, unProceso);
		pthread_mutex_unlock(&mutexColaEx);
		memcpy(impresion + tamanioTexto, &(unProceso->pcb->programId), 4);
		Serializar(IMPRESIONPORCONSOLA, tamanioTexto + 4, impresion,
				unProceso->socketCONSOLA);
		free(impresion);
		break;
	}
	case PROCESOWAIT: {
		char* semaforo = malloc(tamanoPaquete);
		memcpy(semaforo, paquete, tamanoPaquete);
		proceso* unProceso;
		pthread_mutex_lock(&mutexColaEx);
		unProceso = sacarProcesoDeEjecucion(socket);
		pthread_mutex_unlock(&mutexColaEx);
		pthread_mutex_lock(&mutexConfig);
		int valorSemaforo = pideSemaforo(semaforo);
		int mandar;
		if (valorSemaforo <= 0) {
			mandar = 1;
			Serializar(PROCESOWAIT, sizeof(int), &mandar, socket);
			procesoBloqueado *unProcesoBloqueado = malloc(
					sizeof(procesoBloqueado));
			unProcesoBloqueado->pid = unProceso->pcb->programId;
			unProcesoBloqueado->semaforo = semaforo;
			pthread_mutex_lock(&mutexProcesosBloqueados);
			queue_push(colaProcesosBloqueados, unProcesoBloqueado);
			pthread_mutex_unlock(&mutexProcesosBloqueados);
			queue_push(colaCpu, socket);
			sem_post(&semCpu);

		} else {
			escribeSemaforo(semaforo, pideSemaforo(semaforo) - 1);
			mandar = 0;
			Serializar(PROCESOWAIT, sizeof(int), &mandar, socket);
			pthread_mutex_lock(&mutexColaEx);
			queue_push(colaExec, unProceso);
			pthread_mutex_unlock(&mutexColaEx);
		}

		pthread_mutex_unlock(&mutexConfig);
		free(semaforo);
		break;
	}
	case PROCESOSIGNAL: {
		proceso* unProceso;
		char* semaforo = malloc(tamanoPaquete);
		memcpy(semaforo, paquete, tamanoPaquete);
		pthread_mutex_lock(&mutexColaEx);
		unProceso = sacarProcesoDeEjecucion(socket);
		queue_push(colaExec, unProceso);
		pthread_mutex_unlock(&mutexColaEx);
		pthread_mutex_lock(&mutexConfig);
		liberaSemaforo(semaforo);
		pthread_mutex_unlock(&mutexConfig);
		free(semaforo);
		break;
	}
	case SEBLOQUEOELPROCESO: {
		programControlBlock* pcbRecibido = deserializarPCB(paquete);
		proceso* unProceso = sacarProcesoDeEjecucionPorPid(
				pcbRecibido->programId);
		destruirPCB(unProceso->pcb);
		char* semaforo = conseguirSemaforoDeBloqueado(pcbRecibido->programId);
		unProceso->pcb = pcbRecibido;
		bloqueoSemaforo(unProceso,semaforo);
		queue_push(colaCpu, socket);
		sem_post(&semCpu);
		break;

	}
		return;
	}
}

char* conseguirSemaforoDeBloqueado(int pid) {
	char* semaforo;
	int a = 0, t;
	procesoBloqueado * unProceso;
	while (unProceso = (procesoBloqueado*) list_get(colaProcesosBloqueados->elements, a)) {
			if (unProceso->pid == pid)
				return unProceso->semaforo;
			// TODO: REVISAR ESOTOOOO
			a++;
		}
}

void sacarSemaforosDesbloqueados(char* semaforo) {
	int a = 0, t;
	procesoBloqueado * unProceso;
	while (unProceso = (procesoBloqueado*) list_get(colaProcesosBloqueados->elements, a)) {
			if (strcmp((char*) unProceso->semaforo, semaforo) == 0)
				list_remove(colaProcesosBloqueados->elements, a);
			// TODO: REVISAR ESOTOOOO
			a++;
		}
}

void abortarTodosLosProgramasDeConsola(int socket) {
	procesoConsola* unProceso;
	bool esMiSocket(void * entrada) {
		procesoConsola * unproceso = (procesoConsola *) entrada;
		return unproceso->consola == socket;
	}
	unProceso = (procesoConsola*) list_remove_by_condition(
			colaProcesosConsola->elements, esMiSocket);
	while (unProceso != NULL) {
		abortarProgramaPorConsola(unProceso->pid, codeDesconexionConsola);
		unProceso = (procesoConsola*) list_remove_by_condition(
				colaProcesosConsola->elements, esMiSocket);

	}
}

void crearPCB(char* codigo, programControlBlock *unPcb) {
	t_metadata_program* metadata_program;
	metadata_program = metadata_desde_literal(codigo);
	processID++;
	unPcb->programId = processID;
	unPcb->exitCode = -1;
	unPcb->programCounter = 0;
	unPcb->tamanoIndiceCodigo = (metadata_program->instrucciones_size);
	unPcb->indiceCodigo = malloc(unPcb->tamanoIndiceCodigo * 2 * sizeof(int));

	for (i = 0; i < metadata_program->instrucciones_size; i++) {
		/*printf("Instruccion inicio:%d offset:%d %.*s",
		 metadata_program->instrucciones_serializado[i].start,
		 metadata_program->instrucciones_serializado[i].offset,
		 metadata_program->instrucciones_serializado[i].offset,
		 codigo + metadata_program->instrucciones_serializado[i].start);*/
		unPcb->indiceCodigo[i * 2] =
				metadata_program->instrucciones_serializado[i].start;
		unPcb->indiceCodigo[i * 2 + 1] =
				metadata_program->instrucciones_serializado[i].offset;
	}
	unPcb->tamanoindiceEtiquetas = metadata_program->etiquetas_size;
	unPcb->indiceEtiquetas = malloc(
			unPcb->tamanoindiceEtiquetas * sizeof(char));
	memcpy(unPcb->indiceEtiquetas, metadata_program->etiquetas,
			unPcb->tamanoindiceEtiquetas * sizeof(char));
	unPcb->indiceStack = list_create();

	indiceDeStack *indiceInicial;
	indiceInicial = malloc(sizeof(indiceDeStack));
	indiceInicial->args = list_create();
	indiceInicial->vars = list_create();
	indiceInicial->tamanoArgs = 0;
	indiceInicial->tamanoVars = 0;
	indiceInicial->pos = 0;
	list_add(unPcb->indiceStack, (void*) indiceInicial);
	unPcb->tamanoIndiceStack = 1;
	metadata_destruir(metadata_program);
}

void enviarProcesoAMemoria(int cantidadDePaginas, char* codigo,
		int tamanoPaquete) {
	int offset = 0;
	int i = 0;
	int sobra = tamanoPaquete % MARCOS_SIZE;
	for (i = 1; i <= cantidadDePaginas; i++) {
		if (i == cantidadDePaginas) {
			int offsetaux = 0;
			void* envioPagina = malloc(MARCOS_SIZE + 4 * sizeof(int));
			char * sobras = "0000000000000000000000";
			memcpy(envioPagina, &processID, sizeof(processID));
			memcpy(envioPagina + 4, &i, sizeof(int));
			memcpy(envioPagina + 8, &MARCOS_SIZE, sizeof(int));
			memcpy(envioPagina + 12, &offsetaux, sizeof(int));
			memcpy(envioPagina + 16, codigo + offset, sobra);
			memcpy(envioPagina + sobra + 16, sobras, MARCOS_SIZE - sobra);
			Serializar(PAGINA, MARCOS_SIZE + 4 * sizeof(int), envioPagina,
					clienteMEM);
			sem_wait(&semPaginaEnviada);
			free(envioPagina);
		} else {
			int offsetaux = 0;
			void* envioPagina = malloc(MARCOS_SIZE + 4 * sizeof(int));
			memcpy(envioPagina, &processID, sizeof(processID));
			memcpy(envioPagina + 4, &i, sizeof(int));
			memcpy(envioPagina + 8, &MARCOS_SIZE, sizeof(int));
			memcpy(envioPagina + 12, &offsetaux, sizeof(int));
			memcpy(envioPagina + 16, codigo + offset, MARCOS_SIZE);
			offset = offset + MARCOS_SIZE;
			Serializar(PAGINA, MARCOS_SIZE + 4 * sizeof(int), envioPagina,
					clienteMEM);
			printf("envio pagina %s\n", envioPagina);
			sem_wait(&semPaginaEnviada);
			free(envioPagina);
		}

	}
	sem_post(&semEnvioPaginas);
}

void planificadorLargoPlazo() {
	proceso* unProcesoPlp;

	while (1) {
		sem_wait(&semNew);
		pthread_mutex_lock(&mutexColaNew);
		unProcesoPlp = queue_pop(colaNew);
		pthread_mutex_unlock(&mutexColaNew);
		pthread_mutex_lock(&mutexColaReady);
		queue_push(colaReady, unProcesoPlp);
		pthread_mutex_unlock(&mutexColaReady);
		sem_post(&semReady);
	}

}

void planificadorCortoPlazo() {
	proceso* unProcesoPcp;
	int socket;
	while (1) {
		sem_wait(&semReady);
		sem_wait(&semCpu);
		pthread_mutex_lock(&mutexColaReady);
		unProcesoPcp = queue_pop(colaReady);
		pthread_mutex_unlock(&mutexColaReady);
		pthread_mutex_lock(&mutexColaCpu);
		socket = (int) queue_pop(colaCpu);
		pthread_mutex_unlock(&mutexColaCpu);
		ejecutar(unProcesoPcp, socket);

	}

}

void ejecutar(proceso* procesoAEjecutar, int socket) {
	procesoAEjecutar->socketCPU = socket;
	pthread_mutex_lock(&mutexColaEx);
	queue_push(colaExec, procesoAEjecutar);
	pthread_mutex_unlock(&mutexColaEx);
	serializarPCB(procesoAEjecutar->pcb, socket, PCB);
	//el problema esta aca
}

proceso* sacarProcesoDeEjecucion(int sock) {
	int a = 0, t;
	proceso *procesoABuscar;
	while (procesoABuscar = (proceso*) list_get(colaExec->elements, a)) {
		if (procesoABuscar->socketCPU == sock)
			return (proceso*) list_remove(colaExec->elements, a);
		a++;
	}
	printf("NO HAY PROCESO\n");
	exit(0);
	return NULL;
}

proceso* sacarProcesoDeEjecucionPorPid(int pid) {
	int a = 0, t;
	proceso *procesoABuscar;
	while (procesoABuscar = (proceso*) list_get(colaExec->elements, a)) {
		if (procesoABuscar->pcb->programId == pid)
			return (proceso*) list_remove(colaExec->elements, a);
		a++;
	}
	printf("NO HAY PROCESO\n");
	exit(0);
	return NULL;
}

proceso* buscarProcesoEnEjecucion(int pid) {
	int a = 0, t;
	proceso *procesoABuscar;
	while (procesoABuscar = (proceso*) list_get(colaExec->elements, a)) {
		if (procesoABuscar->pcb->programId == pid)
			return procesoABuscar;
		a++;
	}
	printf("NO HAY PROCESO\n");
	exit(0);
	return NULL;
}

void modificarCantidadDePaginas(int pid) {
	int a = 0, t;
	proceso *procesoABuscar;
	while (procesoABuscar = (proceso*) list_get(colaExec->elements, a)) {
		if (procesoABuscar->pcb->programId == pid) {
			procesoABuscar->pcb->cantidadDePaginas++;
			list_replace(colaExec->elements, a, procesoABuscar);
		}
		a++;
	}
	printf("NO HAY PROCESO\n");
	exit(0);
}

int pideVariable(char *variable) {
	int i;
	for (i = 0;
			i < strlen((char*) t_archivoConfig->SHARED_VARS) / sizeof(char*);
			i++) {
		//TODO: mutex confignucleo
		if (strcmp((char*) t_archivoConfig->SHARED_VARS[i], variable) == 0) {
			return atoi(t_archivoConfig->SHARED_VARS_INIT[i]);
		}
	}
	printf("No encontre variable %s %d id, exit\n", variable, strlen(variable));
//TODO abortar
}

void escribeVariable(char *variable, int valor) {
	int i;
	char str[15];
	sprintf(str, "%d", valor);
	for (i = 0;
			i < strlen((char*) t_archivoConfig->SHARED_VARS) / sizeof(char*);
			i++) {
		if (strcmp((char*) t_archivoConfig->SHARED_VARS[i], variable) == 0) {
			memcpy((t_archivoConfig->SHARED_VARS_INIT[i]), str, sizeof(int));
			return;
		}
	}
	printf("No encontre VAR %s id, exit\n", variable);
	free(variable);
//TODO abortar

}

int pideSemaforo(char *semaforo) {
	int i;
//printf("NUCLEO: pide sem %s\n", semaforo);

	for (i = 0; i < strlen((char*) t_archivoConfig->SEM_IDS) / sizeof(char*);
			i++) {
		if (strcmp((char*) t_archivoConfig->SEM_IDS[i], semaforo) == 0) {

			//if (config_nucleo->VALOR_SEM[i] == -1) {return &config_nucleo->VALOR_SEM[i];}
			//config_nucleo->VALOR_SEM[i]--;
			return atoi(t_archivoConfig->SEM_INIT[i]);
		}
	}
	printf("No encontre SEM id, exit\n");
	exit(0);
}

void escribeSemaforo(char *semaforo, int valor) {
	int i;
	char str[15];
	sprintf(str, "%d", valor);
//printf("NUCLEO: pide sem %s\n", semaforo);

	for (i = 0; i < strlen((char*) t_archivoConfig->SEM_IDS) / sizeof(char*);
			i++) {
		if (strcmp((char*) t_archivoConfig->SEM_IDS[i], semaforo) == 0) {

			//if (config_nucleo->VALOR_SEM[i] == -1) {return &config_nucleo->VALOR_SEM[i];}
			memcpy((t_archivoConfig->SEM_INIT[i]), str, sizeof(int));
			return;
		}
	}
	printf("No encontre SEM id, exit\n");
	exit(0);
}

void liberaSemaforo(char *semaforo) {
	int i;
	proceso *proceso;

	for (i = 0; i < strlen((char*) t_archivoConfig->SEM_IDS) / sizeof(char*);
			i++) {
		if (strcmp((char*) t_archivoConfig->SEM_IDS[i], semaforo) == 0) {

			if (list_size(colas_semaforos[i]->elements)) {
				sacarSemaforosDesbloqueados(semaforo);
				proceso = queue_pop(colas_semaforos[i]);
				pthread_mutex_lock(&mutexColaReady);
				queue_push(colaReady, proceso);
				pthread_mutex_unlock(&mutexColaReady);
				sem_post(&semReady);
			} else {
				//esto deberia sumar 1
				int valor = 1 + atoi(t_archivoConfig->SEM_INIT[i]);
				char str[15];
				sprintf(str, "%d", valor);
				memcpy((t_archivoConfig->SEM_INIT[i]), str, sizeof(int));
			}

			return;
		}
	}
	printf("No encontre SEM id, exit\n");
	exit(0);
}

void bloqueoSemaforo(proceso *proceso, char *semaforo) {
	int i;

	for (i = 0; i < strlen((char*) t_archivoConfig->SEM_IDS) / sizeof(char*);
			i++) {
		if (strcmp((char*) t_archivoConfig->SEM_IDS[i], semaforo) == 0) {

			queue_push(colas_semaforos[i], proceso);
			return;

		}
	}
	printf("No encontre SEM id, exit\n");
	exit(0);
}

t_puntero reservarMemoria(int pid, int tamano) {
	int pagina;
	t_puntero puntero;
	int offset;
	proceso* proceso = buscarProcesoEnEjecucion(pid);
	if (tamano < MARCOS_SIZE - 10) {
		HeapMetaData* heapOcupado = malloc(sizeof(HeapMetaData));
		heapOcupado->isFree = false;
		heapOcupado->size = tamano;
		pagina = existePaginaParaPidConEspacio(pid, tamano);
		if (pagina) {
			pedirAMemoriaElPunteroDeLaPaginaDondeEstaLibre(pagina, pid);
			offset = actualizarPaginaEnMemoria(punteroPaginaHeap, pid, pagina,
					tamano); //actualiza tabla, actualiza mem
		} else {
			HeapMetaData* heapLibre = malloc(sizeof(HeapMetaData));
			heapLibre->isFree = true;
			heapLibre->size = MARCOS_SIZE - 2 * sizeof(HeapMetaData) - tamano;
			pedirAMemoriaUnaPaginaPara(pid,
					proceso->pcb->cantidadDePaginas + 1);
			modificarCantidadDePaginas(pid);
			guardarPaginaEnTabla(proceso->pcb->cantidadDePaginas + 1, pid,
					MARCOS_SIZE - 2 * sizeof(HeapMetaData) - tamano);
			offset = crearPaginaEnMemoria(pid, pagina, tamano);
		}
	}
	puntero = (t_puntero) punteroPaginaHeap + offset;
	return puntero;
}

int existePaginaParaPidConEspacio(int pid, int tamano) {
	int i;
	for (i = 0; i < 100; i++) {
		if (tablaHeap[i].pid == pid && tablaHeap[i].tamanoDisponible >= tamano)
			return tablaHeap[i].numeroPagina;
	}
	return 0;
}

void pedirAMemoriaElPunteroDeLaPaginaDondeEstaLibre(int pagina, int pid) {
	void* envioPedidoPagina = malloc(2 * sizeof(int));
	memcpy(envioPedidoPagina, pagina, sizeof(int));
	memcpy(envioPedidoPagina + sizeof(int), pid, sizeof(int));
	Serializar(PUNTEROPAGINAHEAP, 2 * sizeof(int), envioPedidoPagina,
			clienteMEM);
	sem_wait(&semPunteroPaginaHeap);
	return;
}

void pedirAMemoriaUnaPaginaPara(int pid, int numeroPagina) {
	void* envioPedidoPagina = malloc(2 * sizeof(int));
	memcpy(envioPedidoPagina, pid, sizeof(int));
	memcpy(envioPedidoPagina + sizeof(int), numeroPagina, sizeof(int));
	Serializar(PIDOPAGINAHEAP, 2 * sizeof(int), envioPedidoPagina, clienteMEM);
	sem_wait(&semPunteroPaginaHeap);
}
void guardarPaginaEnTabla(int pagina, int pid, int tamano) {
	tablaHeap[indiceLibreHeap].pid = pid;
	tablaHeap[indiceLibreHeap].numeroPagina = pagina;
	tablaHeap[indiceLibreHeap].tamanoDisponible = MARCOS_SIZE - tamano;
	tablaHeap[indiceLibreHeap].cantidadDeAlocaciones = 2;
	indiceLibreHeap++;
}
int crearPaginaEnMemoria(int pid, int pagina, int tamano) {
	int offset = 0;
	int tamanoAEnviar = sizeof(HeapMetaData);
	HeapMetaData* datosAEnviar = malloc(tamanoAEnviar);
	datosAEnviar->size = tamano;
	datosAEnviar->isFree = false;
	void* estructuraAEscribir = malloc(sizeof(int) * 4 + sizeof(HeapMetaData));
	memcpy(estructuraAEscribir, &pid, sizeof(int));
	memcpy(estructuraAEscribir, &pagina, sizeof(int));
	memcpy(estructuraAEscribir, &offset, sizeof(int));
	memcpy(estructuraAEscribir, &tamanoAEnviar, sizeof(int));
	memcpy(estructuraAEscribir, datosAEnviar->size, sizeof(int));
	printf("estoy haciendo bien las cosas?, %d", &datosAEnviar->isFree);
	memcpy(estructuraAEscribir, &datosAEnviar->isFree, sizeof(_Bool));
	Serializar(ESCRITURAPAGINA, sizeof(int) * 4 + sizeof(HeapMetaData),
			estructuraAEscribir, clienteMEM);
	sem_wait(&semPunteroPaginaHeap);
	void* estructuraAEscribir2 = malloc(sizeof(int) * 4 + sizeof(HeapMetaData));
	datosAEnviar->size = MARCOS_SIZE - tamano - 2 * sizeof(HeapMetaData);
	datosAEnviar->isFree = true;
	memcpy(estructuraAEscribir2, &pid, sizeof(int));
	memcpy(estructuraAEscribir2, &pagina, sizeof(int));
	memcpy(estructuraAEscribir2, &tamano, sizeof(int));
	memcpy(estructuraAEscribir2, &tamanoAEnviar, sizeof(int));
	memcpy(estructuraAEscribir2, datosAEnviar->size, sizeof(int));
//OJO CHEQUEAR SI ESTO SE HACE BIEN
	memcpy(estructuraAEscribir2, &datosAEnviar->isFree, sizeof(_Bool));
	printf("estoy haciendo bien las cosas?, %d", &datosAEnviar->isFree);
	Serializar(ESCRITURAPAGINA, sizeof(int) * 4 + sizeof(HeapMetaData),
			estructuraAEscribir2, clienteMEM);
	sem_wait(&semPunteroPaginaHeap);
	return 5;
}

int actualizarPaginaEnMemoria(char* pagina, int pid, int numeroPagina,
		int tamano) {
	int isFree = false;
	int size = 0;
	int corriendoPagina = 0;
	while (isFree == false) {
		memcpy(pagina + corriendoPagina, &size, sizeof(int));
		memcpy(pagina + 4 + corriendoPagina, &isFree, sizeof(bool));
		if (isFree == false) {
			corriendoPagina += size;
		}
	}
	int tamanoAEnviar = sizeof(HeapMetaData);
	HeapMetaData* datosAEnviar = malloc(tamanoAEnviar);
	datosAEnviar->size = tamano;
	datosAEnviar->isFree = false;
	void* estructuraAEscribir = malloc(sizeof(int) * 4 + sizeof(HeapMetaData));
	memcpy(estructuraAEscribir, &pid, sizeof(int));
	memcpy(estructuraAEscribir, &pagina, sizeof(int));
	memcpy(estructuraAEscribir, &corriendoPagina, sizeof(int));
	memcpy(estructuraAEscribir, &tamanoAEnviar, sizeof(int));
	memcpy(estructuraAEscribir, datosAEnviar->size, sizeof(int));
	memcpy(estructuraAEscribir, &datosAEnviar->isFree, sizeof(_Bool));
	Serializar(ESCRITURAPAGINA, sizeof(int) * 4 + sizeof(HeapMetaData),
			estructuraAEscribir, clienteMEM);
	sem_wait(&semPunteroPaginaHeap);
	int i, tamanoLibre, temOffset;
	for (i = 0; i <= 100; i++) {
		if (tablaHeap[i].pid == pid
				&& tablaHeap[i].numeroPagina == numeroPagina) {
			tablaHeap[indiceLibreHeap].tamanoDisponible -= sizeof(HeapMetaData)
					+ tamano;
			tamanoLibre = tablaHeap[indiceLibreHeap].tamanoDisponible;
			tablaHeap[indiceLibreHeap].cantidadDeAlocaciones++;
		}
	}
	temOffset = corriendoPagina + tamano;
	void* estructuraAEscribir2 = malloc(sizeof(int) * 4 + sizeof(HeapMetaData));
	datosAEnviar->size = tamanoLibre;
	datosAEnviar->isFree = true;
	memcpy(estructuraAEscribir2, &pid, sizeof(int));
	memcpy(estructuraAEscribir2, &pagina, sizeof(int));
	memcpy(estructuraAEscribir2, &temOffset, sizeof(int));
	memcpy(estructuraAEscribir2, &tamanoAEnviar, sizeof(int));
	memcpy(estructuraAEscribir2, datosAEnviar->size, sizeof(int));
	memcpy(estructuraAEscribir2, &datosAEnviar->isFree, sizeof(_Bool));
	Serializar(ESCRITURAPAGINA, sizeof(int) * 4 + sizeof(HeapMetaData),
			estructuraAEscribir2, clienteMEM);
	sem_wait(&semPunteroPaginaHeap);
	return corriendoPagina + 5;
}

void liberarMemoria(int pid, t_puntero unaDireccion) {
//ANTES: GUARDAR PUNTERO y posicion real (offset en la pagina) EN DATOS HEAP como lista
//traerme esa pagina
//buscar en donde est'a ese puntero
//defragmentar..
}

void abortar(proceso *proceso, int exitCode) {
	proceso->pcb->exitCode = exitCode;
	pthread_mutex_lock(&mutexColaExit);
	destruirCONTEXTO(proceso->pcb);
	queue_push(colaExit, proceso);
	pthread_mutex_unlock(&mutexColaExit);
//TODO : liberar recursos de memoria
}

void abortarProgramaPorConsola(int pid, int codigo) {
	proceso* unProceso;
	bool esMiPid(void * entrada) {
		proceso * unproceso = (proceso *) entrada;
		return unproceso->pcb->programId == pid;
	}
	pthread_mutex_lock(&mutexColaReady);
	unProceso = (proceso*) list_remove_by_condition(colaReady->elements,
			esMiPid);
	pthread_mutex_unlock(&mutexColaReady);
	if (unProceso != NULL) {
		log_info(logger, "NUCLEO: Abortado x consola, en ready. Pre wait");
		printf("aborte pid: %d con codigo %d\n", unProceso->pcb->programId,
				codigo);
		//sem_post(&semCpu);
		log_info(logger, "NUCLEO: post wait");
		abortar(unProceso, codigo);
	} else {
		pthread_mutex_lock(&mutexColaEx);
		unProceso = (proceso*) list_find(colaExec->elements, esMiPid);
		pthread_mutex_unlock(&mutexColaEx);

		if (unProceso != NULL) {
			pthread_mutex_lock(&mutexColaEx);
			unProceso = (proceso*) list_remove_by_condition(colaExec->elements,
					esMiPid);
			pthread_mutex_unlock(&mutexColaEx);
			printf("aborte pid: %d con codigo %d\n", unProceso->pcb->programId,
					codigo);
			abortar(unProceso, codigo);
			//TODO esperar a que termine
			log_info(logger,
					"NUCLEO: Abortado x consola, en exec, espero que termine de trabajar");
			unProceso->abortado = true;
		} else {
			int i;
			for (i = 0;
					i < strlen((char*) t_archivoConfig->SEM_IDS) / sizeof(char*);
					i++) {

				unProceso = (proceso*) list_remove_by_condition(
						colas_semaforos[i]->elements, esMiPid);
				if (unProceso != NULL)
					break;

			}
			if (unProceso != NULL) {
				printf("aborte pid: %d con codigo %d\n",
						unProceso->pcb->programId, codigo);
				log_info(logger, "NUCLEO: Abortado x consola, en semaforo");
				abortar(unProceso, codigo);

			} else {
				//no hay en exec, lo busco en semaforos
				int i;
				for (i = 0;
						i
								< strlen((char*) t_archivoConfig->SEM_IDS)
										/ sizeof(char*); i++) {

					unProceso = (proceso*) list_remove_by_condition(
							colas_semaforos[i]->elements, esMiPid);
					if (unProceso != NULL)
						break;

				}
				if (unProceso != NULL) {
					printf("aborte pid: %d con codigo %d\n",
							unProceso->pcb->programId, codigo);
					log_info(logger, "NUCLEO: Abortado x consola, en semaforo");
					abortar(unProceso, codigo);
				}
			}
		}
	}
}

