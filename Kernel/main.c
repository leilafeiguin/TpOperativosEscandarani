#include "main.h"

struct sockaddr_in direccionCliente;
uint32_t tamanoDireccion;
int32_t fdmax;
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
char buf[3];    // buffer para datos del cliente
int32_t servidor;
int32_t activado;
int32_t clientefs;
int32_t bytesRecibidos;
int32_t tamanoPaquete;
int32_t processID = 0;
struct sockaddr_in direccionFs;
struct sockaddr_in direccionServidor;

int32_t main(int argc, char**argv) {
	configuracion(argv[1]);
	conectarConMemoria();
	ConectarConFS();
	levantarServidor();
	return EXIT_SUCCESS;
}

void configuracion(char*dir) {
	t_archivoConfig = malloc(sizeof(archivoConfigKernel));
	configuracionKernel(t_archivoConfig, config, dir);
}
int32_t conectarConMemoria() {
	llenarSocketAdrrConIp(&direccionMem, t_archivoConfig->IP_MEMORIA,
			t_archivoConfig->PUERTO_MEMORIA);
	clienteMEM = socket(AF_INET, SOCK_STREAM, 0);
	if (connect(clienteMEM, (void*) &direccionMem, sizeof(direccionMem)) != 0) {
		perror("No se pudo conectar con memoria\n");
		return 1;
	}
	int noInteresa;
	Serializar(KERNEL, 4, noInteresa, clienteMEM);
	paquete* paqueteRecibido = Deserializar(clienteMEM);
	if (paqueteRecibido->header < 0) {
		perror("Kernel se desconectó");
		return 1;
	}

	procesar(paqueteRecibido->package, paqueteRecibido->header, tamanoPaquete);

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
	int noInteresa;
	Serializar(KERNEL, 4, noInteresa, clientefs);
	paquete* paqueteRecibido = Deserializar(clientefs);
	if (paqueteRecibido->header < 0) {
		perror("Kernel se desconectó");
		return 1;
	}

	procesar(paqueteRecibido->package, paqueteRecibido->header, tamanoPaquete);
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

void procesar(char * paquete, int32_t id, int32_t tamanoPaquete, int32_t socket) {
	switch (id) {
	case ARCHIVO: {
		printf("%s\n", paquete);
		printf("%d\n", tamanoPaquete);
		paquete[tamanoPaquete] = '\0';
		int cantidadDePaginas = ceil(tamanoPaquete / 20);
		Serializar(TAMANO, 4, &cantidadDePaginas, clienteMEM);
		recv(clienteMEM, &header, sizeof(header), 0);
		if (header == OK) {
			programControlBlock *unPcb = malloc(sizeof(programControlBlock));
			unPcb->cantidadDePaginas = cantidadDePaginas;
			crearPCB(paquete, unPcb);
			Serializar(PID, 4, &processID, socket);
			int offset = 0;
			for (i = 0; i < cantidadDePaginas; i++) {
				void* envioPagina = malloc(24);
				memcpy(envioPagina, paquete + offset, 20);
				memcpy(envioPagina + 20, &processID, sizeof(processID));
				offset = offset + 20;
				printf("%s\n", envioPagina);
				Serializar(PAGINA, 24, envioPagina, clienteMEM);
				recv(clienteMEM, &header, sizeof(header), 0);
				printf("Se enviaron las paginas a memoria\n");
				free(envioPagina);
			}
		}
		break;
	}
	case FILESYSTEM: {
		printf("Se conecto FS\n");
		break;
	}
	case KERNEL: {
		printf("Se conecto Kernel\n");
		break;
	}
	case CPU: {
		printf("Se conecto CPU\n");
		break;
	}
	case CONSOLA: {
		printf("Se conecto Consola\n");
		break;
	}
	case MEMORIA: {
		printf("Se conecto memoria\n");
		break;
	}
	case CODIGO: {

	}
	case OK: {
	}
	}
}

void crearPCB(char* codigo, programControlBlock *unPcb) {
	t_metadata_program* metadata_program;
	metadata_program = metadata_desde_literal(codigo);
	unPcb->programId = processID;
	processID++;
	unPcb->programCounter = 0;
	int tamanoIndiceCodigo = (metadata_program->instrucciones_size);
	unPcb->indiceCodigo = malloc(tamanoIndiceCodigo * 2 * sizeof(int));

	for (i = 0; i < metadata_program->instrucciones_size; i++) {
		printf("Instruccion inicio:%d offset:%d %.*s",
				metadata_program->instrucciones_serializado[i].start,
				metadata_program->instrucciones_serializado[i].offset,
				metadata_program->instrucciones_serializado[i].offset,
				codigo + metadata_program->instrucciones_serializado[i].start);
		unPcb->indiceCodigo[i * 2] =
				metadata_program->instrucciones_serializado[i].start;
		unPcb->indiceCodigo[i * 2 + 1] =
				metadata_program->instrucciones_serializado[i].offset;
	}
	int tamanoEtiquetas = metadata_program->etiquetas_size;
	unPcb->indiceEtiquetas = malloc(tamanoEtiquetas * sizeof(char));
	memcpy(unPcb->indiceEtiquetas, metadata_program->etiquetas,
			tamanoEtiquetas * sizeof(char));
	unPcb->indiceStack = list_create();

	indiceDeStack *indiceInicial;
	indiceInicial = malloc(sizeof(indiceDeStack));
	indiceInicial->args = list_create();
	indiceInicial->vars = list_create();
	indiceInicial->pos = 0;
	list_add(unPcb->indiceStack, (void*) indiceInicial);

	metadata_destruir(metadata_program);
}

