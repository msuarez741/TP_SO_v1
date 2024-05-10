#include <utils/utilsCliente.h>
#include <utils/utilsServer.h>
#include <commons/config.h>
#include <utils/config.h>
#include <commons/string.h>
#include <commons/collections/queue.h>
#include <semaphore.h>

typedef enum {
    NEW, 
    READY, 
    RUNNING, 
    BLOCKED, 
    EXIT
} e_estado_proceso;

typedef struct
{
    char* puerto_escucha;
    char* ip_memoria;
    char* puerto_memoria;
    char* ip_cpu;
    char* puerto_cpu_dispatch;
    char* puerto_cpu_interrupt;
    char* algoritmo_planificacion;
    int quantum;
    //char** recursos;
    //char** instancias; 
    //int grado_multiprogramacion;
} config_struct;

typedef struct {
    uint32_t pid;
    uint32_t program_counter;
    uint8_t quantum;
    //t_registros_cpu registros;
    e_estado_proceso estado;
} t_pcb;

typedef struct {
    uint8_t FLAG_FINALIZACION;
    uint32_t PID;
} prcs_fin;

typedef void (*fc_puntero)();

void cargar_config_struct_KERNEL(t_config*);
void* consola_kernel(void*);
void* planificar_corto_plazo(void*);
void* planificar_largo_plazo(void*);
void* planificar_new_to_ready(void*);
void* planificar_all_to_exit(void*);

void crear_colas();
void destruir_colas();

void* buscar_pcb_por_pid(uint32_t);
t_queue* obtener_cola(uint32_t);
t_registros_cpu obtener_registros();

uint32_t iniciar_proceso(char*);

fc_puntero obtener_algoritmo_planificacion();
void algortimo_fifo();
void algoritmo_rr();
void algoritmo_vrr();