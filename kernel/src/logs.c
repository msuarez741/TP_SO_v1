#include "logs.h"

void log_iniciar_proceso(t_log* logger, uint32_t pid){
    char* mensaje = (char*)malloc(128);
    sprintf(mensaje, "Se crea el proceso %d en NEW", pid);
    log_info(logger, "%s", mensaje);
    free(mensaje);
}
void log_cambio_estado_proceso(t_log* logger, uint32_t pid, char* estado_anterior, char* estado_actual){
    char* mensaje = (char*)malloc(128);
    sprintf(mensaje, "PID: %d - Estado Anterior: %s - Estado Actual: %s", pid, estado_anterior, estado_actual);
    log_info(logger, "%s", mensaje);
    free(mensaje);
}

void log_finaliza_proceso(t_log* logger, uint32_t pid, char* motivo){
    char* mensaje = (char*)malloc(128);
    sprintf(mensaje, "Finaliza el proceso: %d - Motivo: %s", pid, motivo);
    log_info(logger, "%s", mensaje);
    free(mensaje);
}

void log_desalojo_fin_de_quantum(t_log* logger, uint32_t pid){
    char* mensaje = (char*)malloc(128);
    sprintf(mensaje, "PID: %d - Desalojado por fin de Quantum", pid);
    log_info(logger, "%s", mensaje);
    free(mensaje);
}

void log_ingreso_ready(t_log* logger, t_mqueue* cola_ready){
/*    char* mensaje = (char*)malloc(128);
    char* listado_pid = (char*)malloc(128);
    int max = queue_size(cola_ready->cola); 
    char* pid_str = (char*)malloc(20);
    for(int i = 0; i<max; i++){   
        sem_wait(&(cola_ready->mutex));
        sprintf(pid_str, ((list_get(cola_ready->cola->elements, i))->pid));
        sem_post(&(cola_ready->mutex));
        strcat(listado_pid, ", ");
        strcat(listado_pid, pid_str);
    }
    
    sprintf(mensaje, "Cola Ready %p: [%s]", (void*)cola_ready, listado_pid);
    log_info(logger, "%s", mensaje);
    free(mensaje);
    free(listado_pid);
    free(pid_str);
*/} //revisar!!!