#ifndef MAIN_H_
#define MAIN_H_


#include<stdio.h>
#include<stdlib.h>
#include <string.h>
#include <commons/config.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <conexiones.c>
#include <configuracion.h>



void Configuracion(char *);
int32_t ConectarConKernel();
int32_t conectarConMemoria();




//Codigo PCB, implementar en Shared
typedef struct{
	int indice;
	int desplazamiento;
}indiceDeCodigo[];

typedef struct{
	char* nombreEtiqueda;
	int programCounter;
}indiceDeEtiquetas[];

typedef struct {
	int pag;
	int pos;
	int off;
}posicionMemoria;

typedef struct {
	char* ID;
	posicionMemoria pos;
}args;

typedef struct {
	char* ID;
	posicionMemoria pos;
}vars;

typedef struct {
	args args;
	vars vars;
	int retPos;
	posicionMemoria retVar;
}unIndiceDeStack;

typedef struct{
	unIndiceDeStack* unIndiceDeStack;
}indiceDeStack[];

typedef struct{
	int32_t programId;
	int32_t programCounter;
	int32_t cantidadDePaginas;
	int32_t exitCode;
	indiceDeCodigo* indiceCodigo;
	indiceDeEtiquetas* 	indiceEtiquetas;
	indiceDeStack* indiceStack;
}programControlBlock;


#endif
