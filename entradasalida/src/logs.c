#include "logs.h"

void log_operacion(t_log* logger, uint32_t pid, char* instruccion){
    log_info("PID: %d - Operacion: %s", pid, instruccion);
}

void log_crear_archivo(t_log* logger, uint32_t pid, char* nombre_archivo){
    log_info("PID: %d - Crear Archivo: %s", pid, nombre_archivo);
}

void log_eliminar_archivo(t_log* logger, uint32_t pid, char* nombre_archivo){
    log_info("PID: %d - Eliminar Archivo: %s", pid, nombre_archivo);
}

void log_truncar_archivo(t_log* logger, uint32_t pid, char* nombre_archivo, int tamanio){
    log_info("PID: %d - Truncar Archivo: %s - Tamaño: %d", pid, nombre_archivo, tamanio);
}

void log_leer_archivo(t_log* logger, uint32_t pid, char* nombre_archivo, int tamanio, void* puntero){
    log_info("PID: %d - Leer Archivo: %s - Tamaño a Leer: %d - Puntero Archivo: %p", pid, nombre_archivo, tamanio, (void*) puntero);
}

void log_escribir_archivo(t_log* logger, uint32_t pid, char* nombre_archivo, int tamanio, void* puntero){
    log_info("PID: %d - Escribir Archivo: %s - Tamaño a Escribir: %d - Puntero Archivo: %p", pid, nombre_archivo, tamanio, (void*) puntero);
}

void inicio_compactacion(t_log* logger, uint32_t pid){
    log_info("PID: %d - Inicio Compactación.", pid);
}
void fin_compactacion(t_log* logger, uint32_t pid){
    log_info("PID: %d - Fin Compactación.", pid);
}