#include "main.h"
archivoConfigMemoria* t_archivoConfig;
t_config *config;
struct sockaddr_in direccionServidor;
int32_t servidor;
int32_t activado;
int32_t cliente;
int noIMporta =0;
int32_t header;
int clienteCpu;
struct sockaddr_in direccionCliente;
uint32_t tamanoDireccion;
char* buffer;
int32_t tamanoPaquete;
int32_t opcion;
cache cache1;
infoNodoCache* punteroCache;
cacheLru* punteroUsos;
int32_t frameCache=0;
frame frameGeneral;
int32_t tamanoFrame;
cacheLru nodoUso;
//infoTablaMemoria tablaMemoria[500];
infoTablaMemoria* punteroMemoria;
int32_t indiceTabla = 0;
int32_t indiceCache=0;
infoTablaMemoria nodoTablaMemoria;
int32_t numeroPagina = 0;
int32_t pidAnt = -1;
int32_t pidAntCache = -1;
infoNodoCache nodoCache;
int32_t entradasPid=-1;
int32_t entradasCache=0;
int32_t desplazamientoFrame = 0;

pthread_t hiloLevantarConexion;
int32_t idHiloLevantarConexion;

pthread_t hiloCpu;
pthread_t hiloKernel;
pthread_t hiloAtender;
int32_t idHiloCpu;

pthread_t hiloLeerComando;
int32_t idHiloLeerComando;
sem_t semPaginas;
pthread_mutex_t mutexProcesar;

int32_t main(int argc, char**argv) {

	printf("memoria \n");
	configuracion(argv[1]);
	infoTablaMemoria tablaMemoria[t_archivoConfig->MARCOS];
	infoNodoCache tablaCache[t_archivoConfig->ENTRADAS_CACHE];
	cacheLru tablaUsos[t_archivoConfig->ENTRADAS_CACHE];
	punteroMemoria = tablaMemoria;
	punteroCache = tablaCache;
	punteroUsos = tablaUsos;
	inicializarMemoria();
	inicializarOverflow(t_archivoConfig->MARCOS);
	//hacer un malloc , como memoria, hago un malloc del lugar de memoria ademas de tener una tabla
	crearFrameGeneral();
	crearCache();
	idHiloLevantarConexion = pthread_create(&hiloLevantarConexion, NULL,
			levantarConexion, NULL);
	idHiloLeerComando = pthread_create(&hiloLeerComando, NULL, leerComando,
	NULL);

	pthread_join(hiloLevantarConexion, NULL);

	pthread_join(hiloLeerComando, NULL);
	return EXIT_SUCCESS;
}

void configuracion(char *dir) {
	t_archivoConfig = malloc(sizeof(archivoConfigMemoria));
	configuracionMemoria(t_archivoConfig, config, dir);

}

int32_t levantarConexion() {
	llenarSocketAdrr(&direccionServidor, t_archivoConfig->PUERTO);

	servidor = socket(AF_INET, SOCK_STREAM, 0);

	activado = 1;
	setsockopt(servidor, SOL_SOCKET, SO_REUSEADDR, &activado, sizeof(activado));

	if (bind(servidor, (void*) &direccionServidor, sizeof(direccionServidor))
			!= 0) {
		perror("Falló el bind");
		return 1;
	}

	printf("Estoy escuchando\n");
	listen(servidor, 100);

	conectarseConKernel();
	atenderConexionesCPu();
}

void conectarseConKernel() {
	cliente = accept(servidor, (void*) &direccionCliente, &tamanoDireccion);
	printf("Recibí una conexión en %d!!\n", cliente);
	int envio = t_archivoConfig->MARCOS_SIZE;
	Serializar(MEMORIA, 4, &envio, cliente);

	pthread_create(&hiloKernel, NULL, (void *) atenderKernel, NULL);
	//pthread_join(hiloKernel, NULL);
	return ;
}

void atenderConexionesCPu() {
	while (1) {
		clienteCpu = accept(servidor, (void*) &direccionCliente,
				&tamanoDireccion);
		printf("Recibí una conexión en %d!!\n", clienteCpu);
		int envio = t_archivoConfig->MARCOS_SIZE;
		Serializar(MEMORIA, 4, &envio, clienteCpu);

		pthread_create(&hiloAtender, NULL, (void *) atenderCpu, clienteCpu);

		pthread_join(hiloAtender, NULL);
	}

}

int atenderCpu(int socket) {
	while (1) {
		paquete* paqueteRecibido = Deserializar(socket);
		if (paqueteRecibido->header == -1 || paqueteRecibido->header == -2) {
			perror("El chabón se desconectó\n");
			return 1;
		}
		pthread_mutex_lock(&mutexProcesar);
		procesar(paqueteRecibido->package, paqueteRecibido->header,
				paqueteRecibido->size, socket);

		pthread_mutex_unlock(&mutexProcesar);

	}
	return 0;
}

int atenderKernel() {
	while (1) {
		paquete* paqueteRecibido = Deserializar(cliente);
		if (paqueteRecibido->header == -1 || paqueteRecibido->header == -2) {
			perror("El chabón se desconectó\n");
			return 1;
		}

		pthread_mutex_lock(&mutexProcesar);
		procesar(paqueteRecibido->package, paqueteRecibido->header,
				paqueteRecibido->size, cliente);

		pthread_mutex_unlock(&mutexProcesar);

	}
	return 0;
}



void leerComando() {
	while (1) {
		printf("\nIngrese comando\n"
				"1: dump\n"
				"2: buscar frame\n"
				"3: leer de pagina\n"
				"4: escribir en pagina\n"
				"5: size\n"
				"6: liberar pagina de proceso\n");
		scanf("%d", &opcion);
		switch (opcion) {
		case 1: {
			dump();
			break;
		}
		case 2: {
			int32_t pid;
			int32_t pagina;
			printf("ingresar pid\n");
			scanf("%d", &pid);
			printf("ingresar pagina\n");
			scanf("%d", &pagina);
			int32_t unFrame = buscarFrame(pid, pagina);
			printf("el frame correspondiente: ");
			printf("%d\n", unFrame);
			break;
		}
		case 3: {
			int32_t pid;
			int32_t pagina;
			int32_t offset;
			int32_t tamano;
			printf("ingresar pid\n");
			scanf("%d", &pid);
			printf("ingresar pagina\n");
			scanf("%d", &pagina);
			printf("ingresar offset\n");
			scanf("%d", &offset);
			printf("ingresar tamano\n");
			scanf("%d", &tamano);
			char* conten = leerDeCache(pid,pagina,offset,tamano);
			if(conten=='\0'){
				conten = leerDePagina(pid, pagina, offset, tamano);
				escribirEnCache(pid,pagina,offset,tamano,conten);
			}
			/*else {
				conten = leerDeCache(pid, pagina, offset, tamano);
			}*/
			printf("%s/n", conten);
			break;
		}

		case 4: {
			int32_t pid;
			int32_t pagina;
			int32_t offset;
			int32_t tamano;
			//char* contenido = malloc(32); pq de 32 y aca
			printf("ingresar pid\n");
			scanf("%d", &pid);
			printf("ingresar pagina\n");
			scanf("%d", &pagina);
			printf("ingresar offset\n");
			scanf("%d", &offset);
			printf("ingresar tamano\n");
			scanf("%d", &tamano);
			char* contenido = malloc(tamano);
			printf("ingresar contenido\n");
			scanf("%s", contenido);
			escribirEnCache(pid,pagina,offset,tamano,contenido);
			escribirEnPagina(pid, pagina, offset, tamano, contenido);
			break;
		}
		case 5: {
			size();
			break;
		}
		case 6: {
			int32_t pid;
			int32_t pagina;
			printf("ingresar pid\n");
			scanf("%d", &pid);
			printf("ingresar pagina\n");
			scanf("%d", &pagina);
			liberarPaginaDeProceso(pid,pagina);
			break;

		}

		}
	}
}

void procesar(char * paquete, int32_t id, int32_t tamanoPaquete, int32_t socket) {
	int numero_pagina, offset, tamanio, pid_actual;
	switch (id) {
	case ARCHIVO: {
		printf("%s", paquete);
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
		break;
	}
	case TAMANO: {
		int32_t paginas = (int) (*paquete);

		if (paginas > 0) {
			//if (frameGeneral.tamanioDisponible - (paginas*20) >= 0){
			int32_t paginasNegativas = -paginas;
			sem_init(&semPaginas, 0, &paginasNegativas);
			int i;
			if(paginas*t_archivoConfig->MARCOS_SIZE > frameGeneral.tamanioDisponible){
				Serializar(NOENTROPROCESO, 4, &noIMporta, socket);
			}
			else{
			Serializar(ENTRAPROCESO, 4, &noIMporta, socket);
			}
			//}
		}
		break;
	}
	case INICIALIZARPROCESO: {
		int pid;
		int cantidadDePaginas;
		memcpy(&pid, paquete, 4);
		memcpy(&cantidadDePaginas, paquete +4, 4);
		inicializarPrograma(pid,cantidadDePaginas);
		break;
	}
	case PAGINA: {
		int32_t pid;
		int32_t numeroPagina;
		int32_t offset;
		int32_t tamano;
		char *codigoPagina = malloc(t_archivoConfig->MARCOS_SIZE);
		printf("%s\n", paquete);


		//pagina[t_archivoConfiheaderg->MARCOS_SIZE] = '\0';
		memcpy(&pid, paquete, sizeof(int));
		memcpy(&numeroPagina, paquete+4, sizeof(int));
		memcpy(&tamano, paquete+8, sizeof(int));
		memcpy(&offset, paquete+12, sizeof(int));
		memcpy(codigoPagina, paquete+16, t_archivoConfig->MARCOS_SIZE);
		escribirEnPagina(pid,numeroPagina,offset,tamanoPaquete,codigoPagina);
		//printf("pagina: %s\n", pagina);
		//printf("pid: %d\n", pid);
		Serializar(PAGINAENVIADA, 4, &noIMporta, socket);
		//tamanoPaquete es la cantidad de paginas que necesito


		//almacernarPaginaEnFrame(pid, tamanoPaquete, paquete);
		break;
	}
	case LEERSENTENCIA: {
		memcpy(&numero_pagina, paquete, sizeof(int));
		memcpy(&offset, paquete + sizeof(int), sizeof(int));
		memcpy(&tamanio, paquete + sizeof(int) * 2, sizeof(int));
		printf("[LEERSENTENCIA]Quiero leer en la direccion: %d %d %d y le voy a enviar a socket: %d\n",
							numero_pagina, offset,
							offset, socket);
		char * contenido = leerDePagina(1, numero_pagina, offset, tamanio);
		//TODO HARCODEADO PIDDDDDDDD
		//printf("lei: %s\n", contenido);
		Serializar(LEERSENTENCIA, tamanio, contenido, socket);
		//ojo pid actual
		break;
	}
	case DEREFERENCIAR: {
			memcpy(&numero_pagina, paquete, sizeof(int));
			memcpy(&offset, paquete + sizeof(int), sizeof(int));
			memcpy(&tamanio, paquete + sizeof(int) * 2, sizeof(int));
			printf("Quiero leer en la direccion: %d %d %d y le voy a enviar a socket: %d\n",
								numero_pagina, offset,
								offset, socket);
			char * contenido = leerDePagina(1, numero_pagina, offset, tamanio);
			//TODO HARCODEADO PIDDDDDDDD
			//printf("lei: %s\n", contenido);
			Serializar(DEREFERENCIAR, tamanio, contenido, socket);
			//ojo pid actual
			break;
		}
	case ESCRIBIRVARIABLE: {
		memcpy(&numero_pagina, paquete, sizeof(int));
		memcpy(&offset, paquete + sizeof(int), sizeof(int));
		memcpy(&tamanio, paquete + sizeof(int) * 2, sizeof(int));

		buffer = malloc(tamanio);

		memcpy(buffer, paquete + sizeof(int) * 3, tamanio);

		escribirEnPagina(1, numero_pagina, offset, tamanio, buffer);


		Serializar(ESCRIBIRVARIABLE, sizeof(int), &noIMporta, socket);
		sem_post(&semPaginas);
		free(buffer);
		break;
	}
	}

}

void crearFrameGeneral() {

	int32_t tamanioMarcos, cantidadMarcos;
	cantidadMarcos = t_archivoConfig->MARCOS;
	tamanioMarcos = t_archivoConfig->MARCOS_SIZE;
	tamanoFrame = tamanioMarcos;
	frameGeneral.id = 1;
	frameGeneral.tamanio = cantidadMarcos * tamanioMarcos;
	frameGeneral.tamanioDisponible = frameGeneral.tamanio;
	frameGeneral.tamanioOcupado = 0;
	frameGeneral.puntero = malloc(cantidadMarcos * tamanioMarcos);
	frameGeneral.punteroDisponible = frameGeneral.puntero;
	frameGeneral.framesLibres = cantidadMarcos;

}
void crearCache(){

	int32_t cantidadEntradas, tamanioMarcos;
	tamanioMarcos = t_archivoConfig->MARCOS_SIZE;
	cantidadEntradas = t_archivoConfig->ENTRADAS_CACHE;
	cache1.tamanio = cantidadEntradas * tamanioMarcos;
	cache1.tamanioDisponible = cache1.tamanio;
	cache1.puntero = malloc(cache1.tamanio);
	cache1.punteroDisponible = cache1.puntero;
}
void inicializarMemoria(){
	int32_t i;
	for(i=0;i<=t_archivoConfig->MARCOS;i++){
		(punteroMemoria+i)->pid =0;
		(punteroMemoria+i)->numeroPagina =0;
	}
}
void dump() {
	t_log * log;
	log = log_create("dump.log", "Memoria", 0, LOG_LEVEL_INFO);
	log_info(log, "Tamanio de cache %d", t_archivoConfig->ENTRADAS_CACHE);
	log_info(log, "Tamanio disponible de cache %d", 5);// 5hardcodeado
	int32_t i;
	for (i = 0; i <= 500; i++) {
		if ((punteroMemoria+ i)->pid > 0) {
			log_info(log, "numero de frame %d", i);
			log_info(log, "pid %d", (punteroMemoria+ i)->pid);
			log_info(log, "numero de pagina %d", (punteroMemoria + i)->numeroPagina);
		}
	}

}

void size() {
	int32_t size;
	printf("size: 0 memoria 1 proceso\n ");
	scanf("%d", &size);
	switch (size) {
	case 0: {
		printf("tamanio total memoria %d\n", frameGeneral.tamanio);
		printf("tamanio disponible memoria %d\n",
				frameGeneral.tamanioDisponible);
		printf("tamanio ocupado memoria %d\n", frameGeneral.tamanioOcupado);
		break;
	}
	case 1: {
		break;
	}

	}
}

void inicializarPrograma(int32_t pid, int32_t cantPaginas){
	int32_t i;
	for(i=0;i<=cantPaginas-1;i++){

		int32_t frame =calcularPosicion(pid,i);
		int32_t libre;
		libre = estaLibre(frame);
		if(libre==1){


			nodoTablaMemoria.numeroPagina = i+1;
			nodoTablaMemoria.pid = pid;
			punteroMemoria[frame] = nodoTablaMemoria;
			frameGeneral.framesLibres--;

		}
		else{
			int32_t frameLibre;
			frameLibre= buscarFrameLibre();
			agregarSiguienteEnOverflow(frame,frameLibre);
			nodoTablaMemoria.numeroPagina = i;
			nodoTablaMemoria.pid = pid;
			punteroMemoria[frameLibre] = nodoTablaMemoria;
			frameGeneral.framesLibres--;
		}
	}
}

int32_t estaLibre(int32_t unFrame){
	if((punteroMemoria+unFrame)->pid ==0 && (punteroMemoria+unFrame)->numeroPagina ==0)
	{
		return 1;
	}
	else
	{
		return 0;
	}
}
int32_t buscarUltimaPag(int32_t pid){
	int32_t ultimaPagina=0;
	int32_t i;
	for(i=0; i<=t_archivoConfig->MARCOS;i++){
		if((punteroMemoria+i)->pid == pid && (punteroMemoria+i)->numeroPagina > ultimaPagina){
			ultimaPagina = (punteroMemoria+i)->numeroPagina;
		}
	}
	return ultimaPagina;
}

void asignarPaginasAProceso(int32_t pid, int32_t cantPaginas){
	//TODO corregir
	int32_t ultimaPag = buscarUltimaPag(pid);
		if(ultimaPag==0){
			printf("el proceso todavia no fue iniciado");
			// TODO no entra el proceso porque todavia no se inicioserializar(NOENTROPROCESO, );
			return;
		}


	int32_t i;
	int32_t cantFL = frameGeneral.framesLibres;
	//int32_t frameLibre;
	if(cantPaginas > cantFL){
		printf("no se podran asignar todas las paginas requeridas");
		return;
	}
	else{
		for(i=0;i<=cantPaginas-1;i++){

				int32_t frame =calcularPosicion(pid,i);
				int32_t libre;
				libre = estaLibre(frame);
				if(libre==1){


					nodoTablaMemoria.numeroPagina = i+ultimaPag;
					nodoTablaMemoria.pid = pid;
					punteroMemoria[frame] = nodoTablaMemoria;

				}
				else{
					int32_t frameLibre;
					frameLibre= buscarFrameLibre();
					agregarSiguienteEnOverflow(frame,frameLibre);
					nodoTablaMemoria.numeroPagina = i+ultimaPag;
					nodoTablaMemoria.pid = pid;
					punteroMemoria[frameLibre] = nodoTablaMemoria;

				}
			}


	}
}

int32_t buscarFrameLibre(){

	int32_t frameLibre  = calcularPosicion(0,0);
	return frameLibre;
}



void almacernarPaginaEnFrame(int32_t pid, int32_t tamanioBuffer, char* buffer) {
	//SIEMPRE LE TIENE QUE LLEGAR TAMANIO<MARCOS_SIZE OJO

	memcpy(frameGeneral.punteroDisponible, buffer, tamanioBuffer);

	if (pid != pidAnt) {
		numeroPagina = 1;
		pidAnt = pid;
	}
	//nodoTablaMemoria.puntero = frameGeneral.tamanioOcupado;
	nodoTablaMemoria.numeroPagina = numeroPagina;

	//frameGeneral.punteroDisponible += t_archivoConfig->MARCOS_SIZE;
	frameGeneral.tamanioOcupado += t_archivoConfig->MARCOS_SIZE;
	frameGeneral.tamanioDisponible -= tamanioBuffer;
	nodoTablaMemoria.pid = pid;
	punteroMemoria[indiceTabla] = nodoTablaMemoria;
	indiceTabla++;

	almacenarFrameEnCache(pid,tamanioBuffer,buffer, numeroPagina);

	numeroPagina++;
	//PROBAR

}
void liberarPaginaDeProceso(int32_t pid, int32_t pagina){
	int32_t frameBorrar = buscarFrame(pid,pagina);
	//int32_t i;
	/*
	for(i = frameBorrar+1; i<=500; i++){
	    punteroMemoria[i-1] = punteroMemoria[i];
	}
	*/
	(punteroMemoria + frameBorrar)->pid = 0;
	(punteroMemoria + frameBorrar)->numeroPagina = 0;
}



int32_t buscarFrame(int32_t pid, int32_t numeroPagina) {
	int32_t i;
	for (i = 0; i <= t_archivoConfig->MARCOS; i++) {
		if ((punteroMemoria + i)->pid == pid
				&& (punteroMemoria+i)->numeroPagina == numeroPagina) {
			return i;
		}
	}

	return -1;

}





char* leerDePagina(int32_t pid, int32_t pagina, int32_t offset, int32_t tamano) {

	//int32_t unFrame = buscarFrame(pid, pagina);
	int32_t unFrame = calcularPosicion(pid,pagina);

	int32_t correcta = esPaginaCorrecta(unFrame,pid,pagina);
	char* contenido = malloc(tamano);
	if(correcta==1){
		int32_t desplazamiento = unFrame * t_archivoConfig->MARCOS_SIZE + offset;
		memcpy(contenido, frameGeneral.puntero + desplazamiento, tamano);
		//contenido[tamano] = '\0';
		return contenido;
	}
	else
	{

		int32_t colision = buscarEnOverflow(unFrame,pid,pagina);
		agregarSiguienteEnOverflow(unFrame, colision);
		int32_t desplazamiento = colision * t_archivoConfig->MARCOS_SIZE +offset;
		memcpy(contenido, frameGeneral.puntero + desplazamiento, tamano);
		return contenido;
	}

}

void escribirEnPagina(int32_t pid, int32_t pagina, int32_t offset,
		int32_t tamano, char* contenido) {

	//int32_t unFrame = buscarFrame(pid, pagina);
	int32_t unFrame = calcularPosicion(pid,pagina);
	int32_t correcta = esPaginaCorrecta(unFrame,pid,pagina);
	if(correcta ==1){
	int32_t desplazamiento = unFrame * t_archivoConfig->MARCOS_SIZE +offset;
	memcpy(frameGeneral.puntero + desplazamiento, contenido, tamano);
	}
	else{

		int32_t colision = buscarEnOverflow(unFrame,pid,pagina);
		agregarSiguienteEnOverflow(unFrame, colision);
		int32_t desplazamiento = colision * t_archivoConfig->MARCOS_SIZE +offset;
		memcpy(frameGeneral.puntero + desplazamiento, contenido, tamano);

	}

}
void almacenarFrameEnCache(int32_t pid, int32_t tamanioBuffer, char* buffer, int32_t pagina){

	nodoCache.pid = pid;
	nodoCache.numeroPagina = pagina;
	nodoCache.inicioContenido = t_archivoConfig->MARCOS_SIZE * indiceCache;
	nodoUso.pid = pid;
	nodoUso.pagina = pagina;
	nodoUso.uso = 0;
	if(entradasCache < t_archivoConfig->ENTRADAS_CACHE){
			if (buscarPidCache(pid)==0) {

				memcpy(cache1.punteroDisponible,buffer,tamanioBuffer);
				entradasPid =0;
				punteroCache[indiceCache] = nodoCache;
				punteroUsos[indiceCache] = nodoUso;
				indiceCache++;
				entradasPid++;
				entradasCache++;

			}
			else if(buscarPidCache(pid)==1 && entradasPid < t_archivoConfig->CACHE_X_PROC){
				memcpy(cache1.punteroDisponible,buffer,tamanioBuffer);
				punteroCache[indiceCache] = nodoCache;
				punteroUsos[indiceCache] = nodoUso;
				indiceCache++;
				entradasPid++;
				entradasCache++;

			}
			else
				{

					printf("proceso no ingreso completo a cache por maximo de entradas\n");
				}
		}
		else
		{
			remplazoLru(nodoCache,buffer);

		}


}

void escribirEnCache(int32_t pid, int32_t pagina, int32_t offset,
		int32_t tamano, char* contenido){
	int32_t  posicionCache;
	posicionCache = buscarPosicionContenido(pid,pagina);
	int32_t desplazamiento =  posicionCache + offset;
	memcpy(cache1.punteroDisponible + desplazamiento,contenido, tamano);
	/*nodoCache.pid = pid;
	nodoCache.numeroPagina = pagina;
	nodoCache.inicioContenido = buscarNodoCache(pid,pagina)*t_archivoConfig->MARCOS_SIZE + offset; // offset donde se va aescribir ese contenido en cache
	*/
	// corregit el offset contenido = tamanoFrame * i ; i++;
	//
	//contenido = leerDePagina(pid,pagina,offset,tamano);

	 /*
	if(entradasCache < t_archivoConfig->ENTRADAS_CACHE){
		if (buscarPidCache(pid)==0) {
			//revisar si mover puntero para memcpy
			memcpy(cache1.punteroDisponible+nodoCache.inicioContenido, contenido, tamano);
			entradasPid =0;
			punteroCache[indiceCache] = nodoCache;
			indiceCache++;
			entradasPid++;
			entradasCache++;
		}
		else if(buscarPidCache(pid)==1 && entradasPid < t_archivoConfig->CACHE_X_PROC){
			memcpy(cache1.punteroDisponible+nodoCache.inicioContenido, contenido, tamano);
			punteroCache[indiceCache] = nodoCache;
			indiceCache++;
			entradasPid++;
			entradasCache++;
		}
		else
			{
				printf("proceso no ingreso a cache por maximo de entradas");
			}
	}
	else
	{
		remplazoLru(nodoCache, contenido);

	}
	*/
}

char* leerDeCache(int32_t pid, int32_t pagina,int32_t offset,int32_t tamano){
	char* contenido= malloc(tamano);

	int32_t  offsetContenido;
	int32_t i;
		for (i = 0; i <= t_archivoConfig->ENTRADAS_CACHE; i++) {
			if ((punteroCache + i)->pid == pid
					&& (punteroCache+i)->numeroPagina == pagina) {
				offsetContenido = (punteroCache+i)->inicioContenido;
				(punteroUsos+i)->uso++;
				memcpy(contenido,cache1.punteroDisponible + offsetContenido+ offset,tamano);
				return contenido;
			}


		}
	contenido='\0';
	return contenido;
}

int32_t buscarPidCache(int32_t pid){
	int32_t i;
	for(i=0;i<= t_archivoConfig->ENTRADAS_CACHE;i++){
		if((punteroCache+ i)->pid == pid){
			return 1;
		}

	}
	return 0;
}
int32_t buscarNodoCache(int32_t pid, int32_t pagina){
	int32_t i;
		for (i = 0; i <= t_archivoConfig->ENTRADAS_CACHE; i++) {
			if ((punteroCache + i)->pid == pid
					&& (punteroCache+i)->numeroPagina == pagina) {
				return i;
			}
		}

		return -1;

}

int32_t buscarPosicionContenido(int32_t pid, int32_t pagina){
	int32_t i;
		for (i = 0; i <= t_archivoConfig->ENTRADAS_CACHE; i++) {
			if ((punteroCache + i)->pid == pid
					&& (punteroCache+i)->numeroPagina == pagina) {
				return (punteroCache+i)->inicioContenido;
			}
		}

		return -1;

}

void remplazoLru(infoNodoCache nodoCache, char* contenido){
	ordenarPorUso();
	int32_t i;
	cacheLru menosUsado = punteroUsos[0];
	int32_t posicionCache;
	posicionCache= buscarNodoCache(menosUsado.pid,menosUsado.pagina);
	for(i = posicionCache+1; i<=t_archivoConfig->ENTRADAS_CACHE; i++){
	    punteroCache[i-1] = punteroCache[i];
	}
	memcpy(cache1.punteroDisponible+posicionCache, contenido, t_archivoConfig->MARCOS_SIZE);

}
void ordenarPorUso(){
	int32_t i;
	int32_t x;
	cacheLru aux;
	 for(i=0;i<=t_archivoConfig->ENTRADAS_CACHE;i++){
	        for(x=i+1;x<=t_archivoConfig->ENTRADAS_CACHE-1;x++){
	        if((punteroUsos+i)->uso > (punteroUsos+x)->uso){
	            aux=punteroUsos[i];
	            punteroUsos[i]=punteroUsos[x];
	           punteroUsos[x]=aux;
	        }
	    }
	}
}

/* Función Hash */
unsigned int calcularPosicion(int pid, int num_pagina) {
	char str1[20];
	char str2[20];
	sprintf(str1, "%d", pid);
	sprintf(str2, "%d", num_pagina);
	strcat(str1, str2);
	CANTIDAD_DE_MARCOS = t_archivoConfig->MARCOS;
	unsigned int indice = atoi(str1) % CANTIDAD_DE_MARCOS;
	return indice;
}

/* Inicialización vector overflow. Cada posición tiene una lista enlazada que guarda números de frames.
 * Se llenará a medida que haya colisiones correspondientes a esa posición del vector. */
void inicializarOverflow(int cantidad_de_marcos) {
	overflow = malloc(sizeof(t_list*) * cantidad_de_marcos);
	int i;
	for (i = 0; i < CANTIDAD_DE_MARCOS; ++i) { /* Una lista por frame */
		overflow[i] = list_create();
	}
}

/* En caso de colisión, busca el siguiente frame en el vector de overflow.
 * Retorna el número de frame donde se encuentra la página. */
int buscarEnOverflow(int indice, int pid, int pagina) {
	int i = 0;
	for (i = 0; i < list_size(overflow[indice]); i++) {
		if (esPaginaCorrecta(list_get(overflow[indice], i), pid, pagina)) {
			return list_get(overflow[indice], i);
		}
	}
}

/* Agrega una entrada a la lista enlazada correspondiente a una posición del vector de overflow */
void agregarSiguienteEnOverflow(int pos_inicial, int nro_frame) {
	list_add(overflow[pos_inicial], nro_frame);
}

/* Elimina un frame de la lista enlazada correspondiente a una determinada posición del vector de overflow  */
void borrarDeOverflow(int posicion, int frame) {
	int i = 0;
	int index_frame;

	for (i = 0; i < list_size(overflow[posicion]); i++) {
		if (frame == (int) list_get(overflow[posicion], i)) {
			index_frame = i;
			i = list_size(overflow[posicion]);
		}
	}

	list_remove(overflow[posicion], index_frame);
}

/* A implementar por el alumno. Devuelve 1 a fin de cumplir con la condición requerida en la llamada a la función */
int esPaginaCorrecta(int pos_candidata, int pid, int pagina) {
	if((punteroMemoria+pos_candidata)->pid == pid
			&& (punteroMemoria+pos_candidata)->numeroPagina == pagina){
		return 1;
	}
	else{
		return 0;
	}

}
