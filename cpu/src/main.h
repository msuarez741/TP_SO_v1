#include <utils/utilsServer.h>
#include <utils/utilsCliente.h>
#include <commons/config.h>

/*
IP_MEMORIA=127.0.0.1
PUERTO_MEMORIA=8002
PUERTO_ESCUCHA_DISPATCH=8006
PUERTO_ESCUCHA_INTERRUPT=8007
CANTIDAD_ENTRADAS_TLB=32
ALGORITMO_TLB=FIFO
*/

<<<<<<< HEAD


=======
>>>>>>> dad80fd4240e0a605870273fd7731968d718e3d5
typedef struct
{
    char* ip_memoria,
    char* puerto_memoria,
    char* puerto_escucha_dispatch, 
    char* puerto_escucha_interrupt, 
    char* cantidad_entradas_tlb,
    char* algoritmo_tlb,
} config_struct;

extern config_struct config;

void cargar_config_struct_CPU(t_config*);