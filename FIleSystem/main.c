#include "main.h"
#define ARCHIVOLOG "/home/utnso/Log/logFileSystem.txt"
unsigned char *mmapDeBitmap;
t_bitarray * bitarray;
archivoConfigFS* t_archivoConfig;
t_config *config;
struct sockaddr_in direccionServidor;
int32_t servidor;
sem_t semConfig;
int32_t activado;
int32_t header;
struct sockaddr_in direccionCliente;
uint32_t tamanoDireccion;
int32_t cliente;
int32_t tamanoPaquete;
char* buffer;
pthread_t hiloLevantarConexion;
int32_t idHiloLevantarConexion;
int noInteresa;

int32_t main(int argc, char**argv) {
	sem_init(&semConfig, 0, 0);
	configuracion(argv[1], argv[2]);
	log= log_create(ARCHIVOLOG, "FileSystem", 0, LOG_LEVEL_INFO);
	log_info(log,"Iniciando FileSystem\n");
	sem_wait(&semConfig);
	bitarray = bitarray_create_with_mode(mmapDeBitmap,
			(t_archivoConfig->TAMANIO_BLOQUES
					* t_archivoConfig->CANTIDAD_BLOQUES)
					/ (8 * t_archivoConfig->TAMANIO_BLOQUES), MSB_FIRST);
	char *nombreArchivoRecibido = string_new();
	string_append(&nombreArchivoRecibido, t_archivoConfig->PUERTO_MONTAJE);
	string_append(&nombreArchivoRecibido, "Metadata/Bitmap.bin");
	FILE *f;
	f = fopen(nombreArchivoRecibido, "wr+");
	int i;
	for (i = 0; i < 5192; i++) {
		fputc(1, f);
	}
	fclose(f);
	log_info(log,"El tamano del bitarray es de : %d\n\n\n",
			bitarray_get_max_bit(bitarray));
	idHiloLevantarConexion = pthread_create(&hiloLevantarConexion, NULL,
			levantarConexion, NULL);
	pthread_join(hiloLevantarConexion, NULL);
	return EXIT_SUCCESS;
}

void inicializarMmap() {

	int size;
	char *nombreArchivoRecibido = string_new();
	string_append(&nombreArchivoRecibido, t_archivoConfig->PUERTO_MONTAJE);
	string_append(&nombreArchivoRecibido, "Metadata/Bitmap.bin");
	int bitmap = open(nombreArchivoRecibido, O_RDWR);
	struct stat mystat;

	if (fstat(bitmap, &mystat) < 0) {
		close(bitmap);
	}

	/* Get the size of the file. */

	mmapDeBitmap = mmap(NULL, mystat.st_size, PROT_WRITE | PROT_READ,
	MAP_SHARED, bitmap, 0);
	close(bitmap);
	sem_post(&semConfig);
}

void printBitmap() {
	int j;
	for (j = 0; j < 32; j++) {
		bool a = bitarray_test_bit(bitarray, j);
		log_info(log,"%i", a);
	}
	log_info(log,"\n");
}

void adx_store_data(const char *filepath, const char *data) {
	FILE *fp = fopen(filepath, "w");
	if (fp != NULL) {
		fputs(data, fp);
		fclose(fp);
	}
}

void adx_store_data_with_size(const char *filepath, const char *data, int size, int offset) {
	FILE* archivo = fopen(filepath, "r+");
	fseek(archivo, offset, SEEK_SET);

	fwrite(data, size, 1, archivo);

		fclose(archivo);
}

char* obtenerBytesDeUnArchivo(FILE* bloque, int offsetQuePido, int sizeQuePido) {
	char* envio = malloc(sizeQuePido);
	fseek(bloque, offsetQuePido, SEEK_SET);
	fread(envio, sizeQuePido, 1, bloque);
	return envio;

}

char** obtArrayDeBloquesDeArchivo(char* ruta) {
	t_config* configuracion_FS = config_create(ruta);

	return config_get_array_value(configuracion_FS, "BLOQUES");

}

int obtTamanioArchivo(char* ruta) {
	t_config* configuracion_FS = config_create(ruta);

	return config_get_int_value(configuracion_FS, "TAMANIO");

}

void configuracion(char * dir, char* dir2) {
	t_archivoConfig = malloc(sizeof(archivoConfigFS));
	configuracionFS(t_archivoConfig, config, dir, dir2);
	inicializarMmap();

	//printBitmap();
}
int32_t levantarConexion() {
	llenarSocketAdrr(&direccionServidor, t_archivoConfig->PUERTO_KERNEL);
	servidor = socket(AF_INET, SOCK_STREAM, 0);
	activado = 1;
	setsockopt(servidor, SOL_SOCKET, SO_REUSEADDR, &activado, sizeof(activado));

	if (bind(servidor, (void*) &direccionServidor, sizeof(direccionServidor))
			!= 0) {
		perror("Falló el bind");
		return 1;
	}
	log_info(log,"Estoy escuchando\n");
	listen(servidor, 100);
	cliente = accept(servidor, (void*) &direccionCliente, &tamanoDireccion);
	Serializar(FILESYSTEM, 4, &noInteresa, cliente);
	log_info(log,"Recibí una conexión en %d!!\n", cliente);
	while (1) {
		paquete* paqueteRecibido = Deserializar(cliente);
		if (paqueteRecibido->header < 0) {
			perror("Kernel se desconectó\n");
			return 1;
		}

		procesar(paqueteRecibido->package, paqueteRecibido->header,
				paqueteRecibido->size);
	}
}

void procesar(char * paquete, int32_t id, int32_t tamanoPaquete) {
	switch (id) {
	case KERNEL: {
		log_info(log,"Se conecto Kernel\n");
		break;
	}
	case VALIDARARCHIVO: {
		int tamanoArchivo;
		int validado;

		memcpy(&tamanoArchivo, paquete, sizeof(int));
		char* nombreArchivo = malloc(
				tamanoArchivo * sizeof(char) + sizeof(char));
		memcpy(nombreArchivo, paquete + 4, tamanoArchivo);
		strcpy(nombreArchivo + tamanoArchivo, "\0");

		char *nombreArchivoRecibido = string_new();
		string_append(&nombreArchivoRecibido, t_archivoConfig->PUERTO_MONTAJE);
		string_append(&nombreArchivoRecibido, "Archivos");
		string_append(&nombreArchivoRecibido, nombreArchivo);
		log_info(log,"%s\n", nombreArchivoRecibido);
		if (access(nombreArchivoRecibido, F_OK) != -1) {
			// file exists
			validado = 1;
			Serializar(VALIDARARCHIVO, 4, &validado, cliente);
		} else {
			// file doesn't exist
			validado = 0;
			Serializar(VALIDARARCHIVO, 4, &validado, cliente);
		}
		break;
	}
	case CREARARCHIVO: {
		FILE *fp;

		int tamanoArchivo;
		int validado;

		memcpy(&tamanoArchivo, paquete, sizeof(int));
		char* nombreArchivo = malloc(
				tamanoArchivo * sizeof(char) + sizeof(char));
		memcpy(nombreArchivo, paquete + 4, tamanoArchivo);
		strcpy(nombreArchivo + tamanoArchivo, "\0");

		char *nombreArchivoRecibido = string_new();
		string_append(&nombreArchivoRecibido, t_archivoConfig->PUERTO_MONTAJE);
		string_append(&nombreArchivoRecibido, "Archivos");
		string_append(&nombreArchivoRecibido, nombreArchivo);
		int j;
		int encontroUnBloque = 0;
		int bloqueEncontrado = 0;
		for (j = 0; j < t_archivoConfig->CANTIDAD_BLOQUES; j++) {

			bool bit = bitarray_test_bit(bitarray, j);
			if (bit == 0) {
				encontroUnBloque = 1;
				bloqueEncontrado = j;
				break;
			}
		}

		if (encontroUnBloque == 1) {
			fp = fopen(nombreArchivoRecibido, "ab+");
			//asignar bloque en el metadata del archivo(y marcarlo como ocupado en el bitmap)
			//escribir el metadata ese del archivo (TAMANO y BLOQUES)

			bitarray_set_bit(bitarray, bloqueEncontrado);

			char *dataAPonerEnFile = string_new();
			string_append(&dataAPonerEnFile, "TAMANIO=");
			string_append(&dataAPonerEnFile, "0");
			string_append(&dataAPonerEnFile, "\n");
			string_append(&dataAPonerEnFile, "BLOQUES=[");
			char* numerito = string_itoa(bloqueEncontrado);
			string_append(&dataAPonerEnFile, numerito);
			string_append(&dataAPonerEnFile, "]");

			adx_store_data(nombreArchivoRecibido, dataAPonerEnFile);

			validado = 1;
			Serializar(CREARARCHIVO, 4, &validado, cliente);
			log_info(log,"Se creo el archivo\n");
		} else {
			validado = 0;
			Serializar(CREARARCHIVO, 4, &validado, cliente);
			log_info(log,"No se creo el archivo\n");
		}
		break;
	}
	case GUARDARDATOS: {
		FILE *fp;

		int tamanoNombreArchivo;
		int validado;
		int puntero;
		int tamanoBuffer;

		memcpy(&tamanoNombreArchivo, paquete, sizeof(int));
		log_info(log,"Tamano nombre archivo:%d\n", tamanoNombreArchivo);
		char* nombreArchivo = malloc(tamanoNombreArchivo);

		memcpy(&puntero, paquete + 4, sizeof(int));
		log_info(log,"Puntero:%d\n", puntero);

		memcpy(&tamanoBuffer, paquete + 8, sizeof(int));
		log_info(log,"Tamano de la data:%d\n", tamanoBuffer);
		void* buffer = malloc(tamanoBuffer);
		int tamanoTotalBuffer = tamanoBuffer;

		memcpy(buffer, paquete + 12, tamanoBuffer);
		//log_info(log,"Data :%s\n", buffer);

		memcpy(nombreArchivo, paquete + 12 + tamanoBuffer, tamanoNombreArchivo);
		strcpy(nombreArchivo + tamanoNombreArchivo, "\0");
		log_info(log,"Nombre archivo:%s\n", nombreArchivo);
		char *nombreArchivoRecibido = string_new();
		string_append(&nombreArchivoRecibido, t_archivoConfig->PUERTO_MONTAJE);
		string_append(&nombreArchivoRecibido, "Archivos/");
		string_append(&nombreArchivoRecibido, nombreArchivo);

		log_info(log,"Toda la ruta :%s\n", nombreArchivoRecibido);
		if (access(nombreArchivoRecibido, F_OK) != -1) {

			char** arrayBloques = obtArrayDeBloquesDeArchivo(
					nombreArchivoRecibido);

			int cantidadBloques = 0;
			while (!(arrayBloques[cantidadBloques] == NULL)) {
				log_info(log,"%s \n", arrayBloques[cantidadBloques]);
				cantidadBloques++;
			}

			log_info(log,"Cantidad de bloques :%d\n", cantidadBloques);

			char *nombreBloque = string_new();
			string_append(&nombreBloque, t_archivoConfig->PUERTO_MONTAJE);
			string_append(&nombreBloque, "Bloques/");
			string_append(&nombreBloque, arrayBloques[cantidadBloques - 1]);
			string_append(&nombreBloque, ".bin");
			log_info(log,"Nombre del ultimo bloque: %s\n", nombreBloque);
			log_info(log,"Tamano del archivo : %d\n",
					(obtTamanioArchivo(nombreArchivoRecibido)));
			log_info(log,"Tamano del bloque: %d\n", t_archivoConfig->TAMANIO_BLOQUES);
			int cantRestante = cantidadBloques
					* t_archivoConfig->TAMANIO_BLOQUES
					- ((obtTamanioArchivo(nombreArchivoRecibido)));
			log_info(log,"Cantidad restante :%d\n", cantRestante);
			if (tamanoBuffer < cantRestante) {
				adx_store_data_with_size(nombreBloque, buffer, tamanoBuffer, puntero);
				char *dataAPonerEnFile = string_new();
				string_append(&dataAPonerEnFile, "TAMANIO=");

				int tamanioArchivoViejoInt = obtTamanioArchivo(
						nombreArchivoRecibido);
				int tamanioNuevo = tamanioArchivoViejoInt + (tamanoTotalBuffer);
				char* tamanioNuevoChar = string_itoa(tamanioNuevo);
				string_append(&dataAPonerEnFile, tamanioNuevoChar);
				string_append(&dataAPonerEnFile, "\n");
				string_append(&dataAPonerEnFile, "BLOQUES=[");
				int z;

				char** arrayBloques2 = obtArrayDeBloquesDeArchivo(
						nombreArchivoRecibido);
				string_append(&dataAPonerEnFile, arrayBloques2[0]);
				int d = 1;
				while (!(arrayBloques2[d] == NULL)) {
					string_append(&dataAPonerEnFile, ",");
					string_append(&dataAPonerEnFile, arrayBloques2[d]);

					d++;
				}
				string_append(&dataAPonerEnFile, "]");
				fclose(fopen(nombreArchivoRecibido, "w"));
				adx_store_data(nombreArchivoRecibido, dataAPonerEnFile);
				validado = 1;
				Serializar(GUARDARDATOS, 4, &validado, cliente);
				//TODO implementar puntero
			} else {
				void* guardar = malloc(cantRestante);
				memcpy(guardar, buffer, cantRestante);
				adx_store_data_with_size(nombreBloque, guardar, cantRestante, puntero);
				free(guardar);
				tamanoBuffer -= cantRestante;
				int cuantosBloquesMasNecesito = (tamanoBuffer)
						/ t_archivoConfig->TAMANIO_BLOQUES;

				if (((tamanoBuffer) % t_archivoConfig->TAMANIO_BLOQUES) > 0) {
					cuantosBloquesMasNecesito++;
				}
				//si no hay mas bloques de los que se requieren hay que hacer un send tirando error
				int i;
				int indiceNuevos = 0;
				int bloquesEncontrados = 0;
				int bloquesNuevos[cuantosBloquesMasNecesito];
				for (i = 0; i < t_archivoConfig->CANTIDAD_BLOQUES; i++) {

					bool bit = bitarray_test_bit(bitarray, i);
					if (bit == 0) {
						if (bloquesEncontrados == cuantosBloquesMasNecesito) {
							break;
						} else {
							bloquesNuevos[indiceNuevos] = i;
							indiceNuevos++;
						}
						bloquesEncontrados++;
					}
				}

				if (bloquesEncontrados >= cuantosBloquesMasNecesito) {
					//guardamos en los bloques deseados

					int s;
					for (s = 0; s < cuantosBloquesMasNecesito; s++) {

						char *nombreBloque = string_new();
						string_append(&nombreBloque,
								t_archivoConfig->PUERTO_MONTAJE);
						string_append(&nombreBloque, "Bloques/");
						char* numerito = string_itoa(bloquesNuevos[s]);
						string_append(&nombreBloque, numerito);
						string_append(&nombreBloque, ".bin");
						int offsetAux = t_archivoConfig->TAMANIO_BLOQUES;
						if (tamanoBuffer > t_archivoConfig->TAMANIO_BLOQUES) {
							//cortar el string

							void* recortado = malloc(
									t_archivoConfig->TAMANIO_BLOQUES);
							memcpy(recortado, buffer + offsetAux,
									t_archivoConfig->TAMANIO_BLOQUES);
							adx_store_data_with_size(nombreBloque, recortado, t_archivoConfig->TAMANIO_BLOQUES, puntero);
							offsetAux += t_archivoConfig->TAMANIO_BLOQUES;
							tamanoBuffer -= t_archivoConfig->TAMANIO_BLOQUES;
							free(recortado);
						} else {
							void* recortado = malloc(tamanoBuffer);
							memcpy(recortado, buffer + offsetAux, tamanoBuffer);
							//mandarlo to do de una
							adx_store_data_with_size(nombreBloque, recortado, tamanoBuffer, puntero);
							free(recortado);
						}

						//actualizamos el bitmap

						bitarray_set_bit(bitarray, bloquesNuevos[s]);

					}

					//actualizamos el metadata del archivo con los nuevos bloques y el nuevo tamano del archivo

					char *dataAPonerEnFile = string_new();
					string_append(&dataAPonerEnFile, "TAMANIO=");

					int tamanioArchivoViejoInt = obtTamanioArchivo(
							nombreArchivoRecibido);
					int tamanioNuevo = tamanioArchivoViejoInt
							+ (tamanoTotalBuffer);
					char* tamanioNuevoChar = string_itoa(tamanioNuevo);
					string_append(&dataAPonerEnFile, tamanioNuevoChar);
					string_append(&dataAPonerEnFile, "\n");
					string_append(&dataAPonerEnFile, "BLOQUES=[");
					int z;

					char** arrayBloques = obtArrayDeBloquesDeArchivo(
							nombreArchivoRecibido);
					int d = 0;
					while (!(arrayBloques[d] == NULL)) {
						string_append(&dataAPonerEnFile, arrayBloques[d]);
						string_append(&dataAPonerEnFile, ",");
						d++;
					}
					for (z = 0; z < cuantosBloquesMasNecesito; z++) {
						char* bloqueString = string_itoa(bloquesNuevos[z]);
						string_append(&dataAPonerEnFile, bloqueString);
						if (!(z == (cuantosBloquesMasNecesito - 1))) {
							string_append(&dataAPonerEnFile, ",");
						}
					}

					string_append(&dataAPonerEnFile, "]");
					fclose(fopen(nombreArchivoRecibido, "w"));
					adx_store_data(nombreArchivoRecibido, dataAPonerEnFile);

					validado = 1;

					Serializar(GUARDARDATOS, 4, &validado, cliente);

				} else {
					validado = 0;

					Serializar(GUARDARDATOS, 4, &validado, cliente);
				}

			}

		} else {
			validado = 0;

			//MANDAR QUE NO EXISTE EL ARCHIVO?
		}
		break;
	}
	case OBTENERDATOS: {
		FILE *fp;

		int tamanoArchivo;
		int validado;
		int offset;
		int size;

		memcpy(&tamanoArchivo, paquete, sizeof(int));
		memcpy(&size, paquete + 4, sizeof(int));
		memcpy(&offset, paquete + 8, sizeof(int));
		char* nombreArchivo = malloc(
				tamanoArchivo * sizeof(char) + sizeof(char));
		memcpy(nombreArchivo, paquete + 12, tamanoArchivo);
		strcpy(nombreArchivo + tamanoArchivo, "\0");

		char *nombreArchivoRecibido = string_new();
		string_append(&nombreArchivoRecibido, t_archivoConfig->PUERTO_MONTAJE);
		string_append(&nombreArchivoRecibido, "Archivos/");
		string_append(&nombreArchivoRecibido, nombreArchivo);
		log_info(log,"Nombre archivo:%s\n", nombreArchivo);
		if (access(nombreArchivoRecibido, F_OK) != -1) {

			int calcularBloqueInicial = offset
					/ t_archivoConfig->TAMANIO_BLOQUES;

			int offsetInicial = offset % t_archivoConfig->TAMANIO_BLOQUES;
			int indice = calcularBloqueInicial;
			fp = fopen(nombreArchivoRecibido, "r");
			char** arrayBloques = obtArrayDeBloquesDeArchivo(
					nombreArchivoRecibido);
			int cantidadBloquesQueNecesito = size
					/ t_archivoConfig->TAMANIO_BLOQUES;
			if ((size % t_archivoConfig->TAMANIO_BLOQUES) != 0) {
				cantidadBloquesQueNecesito++;
			}

			char* infoTraidaDeLosArchivos = string_new();
			int cantidadDeBloquesAux = cantidadBloquesQueNecesito;
			int sizeFaltante = size;
			while (!(arrayBloques[indice] == NULL || sizeFaltante == 0)) {

				int nroBloque = atoi(arrayBloques[indice]);
				char *nombreBloque = string_new();
				string_append(&nombreBloque, t_archivoConfig->PUERTO_MONTAJE);
				string_append(&nombreBloque, "Bloques/");
				string_append(&nombreBloque, arrayBloques[indice]);
				string_append(&nombreBloque, ".bin");

				FILE *bloque = fopen(nombreBloque, "r");
				int sizeQuePido;
				if (sizeFaltante > t_archivoConfig->TAMANIO_BLOQUES) {
					sizeQuePido = t_archivoConfig->TAMANIO_BLOQUES;
					sizeFaltante -= t_archivoConfig->TAMANIO_BLOQUES;
				} else {
					sizeQuePido = sizeFaltante;
					sizeFaltante = 0;
				}
				int offsetQuePido = offsetInicial;
				char* data = obtenerBytesDeUnArchivo(bloque, offsetQuePido,
						sizeQuePido);
				string_append(&infoTraidaDeLosArchivos, data);
				free(data);
				indice++;
			}

			validado = 1;

			int tamanoAMandar = sizeof(int) * strlen(infoTraidaDeLosArchivos);
			void * envio = malloc(tamanoAMandar + 4);
			memcpy(envio, &tamanoAMandar, 4);
			memcpy(envio + 4, infoTraidaDeLosArchivos, tamanoAMandar);

			Serializar(OBTENERDATOS, tamanoAMandar + 4, envio, cliente);
			free(envio);

		} else {
			validado = 0;
			int tamanoAMandar = -1;
			void * envio = malloc(8 + 4);
			memcpy(envio, &tamanoAMandar, 4);
			memcpy(envio + 4, &validado, 4);

			Serializar(OBTENERDATOS, 8, envio, cliente);
			free(envio);
		}

		break;
	}
	case BORRARARCHIVOFS: {
		FILE *fp;

		int tamanoArchivo;
		int validado;

		memcpy(&tamanoArchivo, paquete, sizeof(int));
		char* nombreArchivo = malloc(
				tamanoArchivo * sizeof(char) + sizeof(char));
		memcpy(nombreArchivo, paquete + 4, tamanoArchivo);
		strcpy(nombreArchivo + tamanoArchivo, "\0");

		char *nombreArchivoRecibido = string_new();
		string_append(&nombreArchivoRecibido, t_archivoConfig->PUERTO_MONTAJE);
		string_append(&nombreArchivoRecibido, "Archivos");
		string_append(&nombreArchivoRecibido, nombreArchivo);

		if (access(nombreArchivoRecibido, F_OK) != -1) {

			fp = fopen(nombreArchivoRecibido, "r");
			//poner en un array los bloques de ese archivo para luego liberarlos
			char** arrayBloques = obtArrayDeBloquesDeArchivo(
					nombreArchivoRecibido);

			if (remove(nombreArchivoRecibido) == 0) {

				validado = 1;

				//marcar los bloques como libres dentro del bitmap (recorriendo con un for el array que cree arriba)
				int d = 0;
				while (!(arrayBloques[d] == NULL)) {
					int indice = atoi(arrayBloques[d]);
					bitarray_clean_bit(bitarray, indice);
					d++;
				}
				validado = 1;
				Serializar(BORRARARCHIVOFS, 4, &validado, cliente);

			} else {
				validado = 0;
				Serializar(BORRARARCHIVOFS, 4, &validado, cliente);
			}
		} else {
			validado = 0;
			Serializar(BORRARARCHIVOFS, 4, &validado, cliente);
		}
		break;
	}

	}
}
