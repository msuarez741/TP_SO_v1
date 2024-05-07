#include <stdlib.h>
#include <stdio.h>
#include <utils/hello.h>
#include "main.h"

/* TODO:
REVISAR BIEN QUE FUNCIONE OK COMANDOS DE MULTIPROGRAMACION Y DE INICIAR/DETENER PLANIFICACION (este commit está listo para probar eso)
hilos de largo plazo (uno NEW->READY otro ALL->EXIT) listo.
atender_cliente por cada modulo
corto_plazo
semaforos (plantear: info en lucid.app) listo.
list_find funcion para comparar elementos listo. (¡¡FALTA PROBARLO!!)
comunicacion kernel - memoria - cpu (codigos de operacion, sockets, leer instrucciones y serializar)
instruccion ENUM con una funcion que delegue segun ENUM una funcion a ejecutar
memoria para leer en proceso (ver commmons)
*/

config_struct config;
t_list *pcb_list;
// t_dictionary *pcb_dictionary; // dictionario dinamica que contiene los PCB de los procesos creados
uint32_t pid; // PID: contador para determinar el PID de cada proceso creado

uint8_t PLANIFICACION_PAUSADA;
prcs_fin FINALIZACION;

t_queue *cola_NEW;
t_queue *cola_READY;
t_queue *cola_BLOCKED;
t_queue *cola_RUNNING;
t_queue *cola_EXIT;

sem_t mutex_planificacion_pausada, mutex_cola_ready, contador_grado_multiprogramacion, orden_planificacion;

int main(int argc, char *argv[])
{

    int conexion_cpu_dispatch, conexion_memoria, conexion_cpu_interrupt;
    pthread_t thread_kernel_servidor, thread_kernel_consola, thread_planificador_corto_plazo, thread_planificador_largo_plazo;

    // ------------ ARCHIVOS CONFIGURACION + LOGGER ------------
    t_config *archivo_config = iniciar_config("kernel.config");
    cargar_config_struct_KERNEL(archivo_config);
    logger = log_create("log.log", "Servidor", 0, LOG_LEVEL_DEBUG);

    // -- INICIALIZACION VARIABLES GLOBALES -- //
    pid = 1;
    PLANIFICACION_PAUSADA = 0;

    // -- INICIALIZACION SEMAFOROS -- //
    sem_init(&mutex_planificacion_pausada, 0, 1);
    sem_init(&mutex_cola_ready, 0, 1);
    sem_init(&orden_planificacion, 0, 0);
    sem_init(&contador_grado_multiprogramacion, 0, config_get_int_value(archivo_config, "GRADO_MULTIPROGRAMACION"));

    decir_hola("Kernel");

    // ------------ CONEXION CLIENTE - SERVIDORES ------------
    // conexion puertos cpu
    conexion_cpu_dispatch = crear_conexion(config.ip_cpu, config.puerto_cpu_dispatch);
    log_info(logger, "se conecta a CPU puerto DISPATCH");
    enviar_conexion("Kernel a DISPATCH", conexion_cpu_dispatch);

    conexion_cpu_interrupt = crear_conexion(config.ip_cpu, config.puerto_cpu_interrupt);
    log_info(logger, "se conecta a CPU puerto INTERRUPT");
    enviar_conexion("Kernel a INTERRUPT", conexion_cpu_interrupt);

    // conexion memoria
    conexion_memoria = crear_conexion(config.ip_memoria, config.puerto_memoria);
    log_info(logger, "se conecta a MEMORIA");
    enviar_conexion("Kernel", conexion_memoria);

    // ------------ CONEXION SERVIDOR - CLIENTES ------------
    int socket_servidor = iniciar_servidor(config.puerto_escucha);
    // log_info(logger, config.puerto_escucha);
    log_info(logger, "Server KERNEL iniciado");

    // ------------ HILOS ------------
    // hilo con MULTIPLEXACION a Interfaces I/O
    if (pthread_create(&thread_kernel_servidor, NULL, servidor_escucha, &socket_servidor) != 0)
    {
        log_error(logger, "No se ha podido crear el hilo para la conexion con interfaces I/O");
        exit(EXIT_FAILURE);
    }
    // hilo para recibir mensajes por consola
    if (pthread_create(&thread_kernel_consola, NULL, consola_kernel, archivo_config) != 0)
    {
        log_error(logger, "No se ha podido crear el hilo para la consola kernel");
        exit(EXIT_FAILURE);
    }
    // hilo para planificacion a corto plazo (READY A EXEC)
    if (pthread_create(&thread_planificador_corto_plazo, NULL, planificar_corto_plazo, archivo_config) != 0)
    {
        log_error(logger, "No se ha podido crear el hilo para el planificador corto plazo");
        exit(EXIT_FAILURE);
    }
    // hilo para planificacion a largo plazo (NEW A READY)
    if (pthread_create(&thread_planificador_largo_plazo, NULL, planificar_largo_plazo, archivo_config) != 0)
    {
        log_error(logger, "No se ha podido crear el hilo para el planificador largo plazo");
        exit(EXIT_FAILURE);
    }

    pthread_join(thread_kernel_servidor, NULL);
    pthread_join(thread_kernel_consola, NULL);
    pthread_join(thread_planificador_corto_plazo, NULL);
    pthread_join(thread_planificador_largo_plazo, NULL);

    sem_destroy(&mutex_cola_ready);
    sem_destroy(&mutex_cola_ready);
    sem_destroy(&contador_grado_multiprogramacion);
    sem_destroy(&orden_planificacion);

    log_destroy(logger);
    config_destroy(archivo_config);
    liberar_conexion(conexion_memoria);
    liberar_conexion(conexion_cpu_dispatch);
    liberar_conexion(conexion_cpu_interrupt);

    return EXIT_SUCCESS;
}

// ------------ DEFINICION FUNCIONES KERNEL ------------

void cargar_config_struct_KERNEL(t_config *archivo_config)
{
    config.puerto_escucha = config_get_string_value(archivo_config, "PUERTO_ESCUCHA");
    config.ip_memoria = config_get_string_value(archivo_config, "IP_MEMORIA");
    config.puerto_memoria = config_get_string_value(archivo_config, "PUERTO_MEMORIA");
    config.ip_cpu = config_get_string_value(archivo_config, "IP_CPU");
    config.puerto_cpu_dispatch = config_get_string_value(archivo_config, "PUERTO_CPU_DISPATCH");
    config.puerto_cpu_interrupt = config_get_string_value(archivo_config, "PUERTO_CPU_INTERRUPT");
    config.algoritmo_planificacion = config_get_string_value(archivo_config, "ALGORITMO_PLANIFICACION");
    config.quantum = config_get_int_value(archivo_config, "QUANTUM");
}

// definicion funcion hilo consola
void *consola_kernel(void *archivo_config)
{
    char *leido;

    while (1)
    {
        leido = readline("> ");

        // Verificar si se ingresó algo
        if (strlen(leido) == 0)
        {
            free(leido);
            break;
        }
        char **tokens = string_split(leido, " ");
        char *comando = tokens[0];
        if (comando != NULL)
        {
            if (strcmp(comando, "EJECUTAR_SCRIPT") == 0 && string_array_size(tokens) >= 2)
            {
                char *path = tokens[1];
                if (strlen(path) != 0 && path != NULL)
                {
                    // comprobar existencia de archivo en ruta relativa
                    // ejecutar_script(path);
                    printf("path ingresado (ejecutar_script): %s\n", path);
                }
            } // INICIAR_PROCESO /carpetaProcesos/proceso1
            else if (strcmp(comando, "INICIAR_PROCESO") == 0 && string_array_size(tokens) >= 2)
            {
                char *path = tokens[1];
                uint8_t pid_proceso_iniciado;
                if (strlen(path) != 0 && path != NULL)
                {
                    // solicitar creacion a memoria de proceso
                    // si se crea proceso, iniciar largo plazo

                    pid_proceso_iniciado = iniciar_proceso(path);
                    // ver funcion para comprobar existencia de archivo en ruta relativa en MEMORIA ¿acá o durante ejecución? => revisar consigna

                    // lo agrega en la cola NEW --> consultada desde planificador_largo_plazo, evaluar semáforo.

                    printf("path ingresado (iniciar_proceso): %s\n", path);
                }
            }
            else if (strcmp(comando, "FINALIZAR_PROCESO") == 0 && string_array_size(tokens) >= 2)
            {
                char *pid_char = tokens[1];
                if (strlen(pid_char) != 0 && pid_char != NULL && atoi(pid_char) > 0)
                {
                    // finalizar_proceso(pid);
                    FINALIZACION.FLAG_FINALIZACION = true; // falta que el cpu y los errores puedan activar el flag tambien
                    FINALIZACION.PID = (uint32_t)atoi(pid_char);
                    printf("pid ingresado (finalizar_proceso): %s\n", pid_char);
                }
            }
            else if (strcmp(comando, "MULTIPROGRAMACION") == 0 && string_array_size(tokens) >= 2)
            {
                char *valor = tokens[1];
                if (strlen(valor) != 0 && valor != NULL && atoi(valor) > 0)
                {
                    if (atoi(valor) > config_get_int_value((t_config *)archivo_config, "GRADO_MULTIPROGRAMACION"))
                    {
                        log_info(logger, "multigramacion mayor");
                        int diferencia = atoi(valor) - config_get_int_value((t_config *)archivo_config, "GRADO_MULTIPROGRAMACION");
                        for (int i = 0; i < diferencia; i++)
                            sem_post(&contador_grado_multiprogramacion); // no hace falta otro semaforo para ejecutar esto porque estos se atienden de forma atomica.
                    }
                    else if (atoi(valor) < config_get_int_value(archivo_config, "GRADO_MULTIPROGRAMACION"))
                    {
                        log_info(logger, "multigramacion menor");
                        int diferencia = config_get_int_value((t_config *)archivo_config, "GRADO_MULTIPROGRAMACION") - atoi(valor);
                        for (int i = 0; i < diferencia; i++)
                            sem_wait(&contador_grado_multiprogramacion); // no hace falta otro semaforo para ejecutar esto porque estos se atienden de forma atomica.
                    }
                    config_set_value((t_config *)archivo_config, "GRADO_MULTIPROGRAMACION", valor);
                    config_save((t_config *)archivo_config);
                    printf("grado multiprogramacion cambiado a %s\n", valor);
                }
            }
            else if (strcmp(comando, "DETENER_PLANIFICACION") == 0)
            {
                // si la planificacion ya estaba detenida, no pierdo tiempo de procesamiento de procesos escribiendola de vuelta
                sem_wait(&mutex_planificacion_pausada);
                PLANIFICACION_PAUSADA = 1; // escritura
                sem_post(&mutex_planificacion_pausada);
                printf("detener planificacion\n");
            }
            else if (strcmp(comando, "INICIAR_PLANIFICACION") == 0)
            {
                // si la planificacion ya estaba corriendo, perdería tiempo de procesamiento si la excluyo para sobreescribirla con el mismo valor
                if(PLANIFICACION_PAUSADA != 0) { // como es lectura y sólo puede ocurrir otra lectura simultaneamente, no necesita semaforo (creo ja!)
                    sem_wait(&mutex_planificacion_pausada);
                    PLANIFICACION_PAUSADA = 0; // escritura
                    sem_post(&mutex_planificacion_pausada);
                }
                printf("iniciar planificacion\n");
            }
            else if (strcmp(comando, "PROCESO_ESTADO") == 0)
            {
                // estados_procesos()
                printf("estados de los procesos\n");
            }
        }
        string_array_destroy(tokens);
        free(leido);
    }

    return NULL;
}

void *planificar_corto_plazo(void *archivo_config)
{
    while (1)
    {
        sem_wait(&mutex_planificacion_pausada);
        if (!PLANIFICACION_PAUSADA)
        { // lectura
            sem_post(&mutex_planificacion_pausada);
            sem_wait(&orden_planificacion); // solo se sigue la ejecución si ya largo plazo dejó al menos un proceso en READY
            sem_wait(&mutex_cola_ready);
                // 1. acá dentro del mutex sólo se ejecutaría la función del algoritmo de planificación para sacar el proceso que corresponda de ready y pasarlo a RUNNING
                sleep(3);
                log_info(logger, "estas en corto plazoo");
            sem_post(&mutex_cola_ready);
            sem_post(&contador_grado_multiprogramacion); // libera un espacio en READY
            // 2. se manda el proceso seleccionado a CPU a través del puerto dispatch
            // 3. se aguarda respuesta de CPU para tomar una decisión y continuar con la ejecución de procesos 
        }
        else
            sem_post(&mutex_planificacion_pausada);
    }
}

void *planificar_largo_plazo(void *archivo_config)
{
    pthread_t thread_NEW_READY, thread_ALL_EXIT;

    pthread_create(&thread_NEW_READY, NULL, planificar_new_to_ready, archivo_config);
    pthread_create(&thread_ALL_EXIT, NULL, planificar_all_to_exit, NULL);

    pthread_detach(thread_NEW_READY);
    pthread_detach(thread_NEW_READY);
    return NULL;
}

void *planificar_new_to_ready(void *archivo_config)
{
    while (1)
    {
        sem_wait(&mutex_planificacion_pausada);
        if (!PLANIFICACION_PAUSADA)
        { // lectura
            sem_post(&mutex_planificacion_pausada);
            sem_wait(&contador_grado_multiprogramacion);
            sem_wait(&mutex_cola_ready);
            log_info(logger, "estas en largo plazoo");
            /*
            // NEW -> READY
            if (queue_size(cola_NEW) != 0){
                sem_wait(&contador_grado_multiprogramacion)
                queue_push(cola_READY, queue_peek(cola_NEW));
                queue_pop(cola_NEW);
            }
            */
            sem_post(&mutex_cola_ready);
            sem_post(&orden_planificacion);
        }
        else
        {
            sem_post(&mutex_planificacion_pausada);
        }
    }
}

void *planificar_all_to_exit(void *args)
{
    while (1){   
        // OTRA SOLUCION: 
        /* creo que sería mejor agregar desde cada lugar desde donde se finalice el proceso (corto_plazo o consola) el pcb del proceso a la cola EXIT
        y luego en este hilo trabajar con los procesos que van llegando a la cola EXIT (con semáforos y demás), y trabajar con cada caso:

        Casos que pueden darse por FINALIZAR_PROCESO desde consola:
            A) si el proceso tiene estado RUNNING se manda interrupcion a CPU para que desaloje  
                => al desalojar de CPU se devuelve a kernel (cod_op INTERRUPCION) se actualiza luego su ctx y recién ahí se liberan recursos ¿no?
            B) si el proceso tiene estado READY se lo saca de la cola (con semáforos), se liberan recursos y se aumenta en uno el grado de multiprogramación
            C) si el proceso tiene estado NEW sólo se lo saca de la cola (con semáforos) y se libera memoria
            E) si está bloqueado VER (un quilombo)
        Casos que pueden darse por error (recurso inexistente, i/o no conectada a kernel, ...), es decir, errores durante la ejecución del proceso (corto plazo) o porque se finalizó el proceso correctamente (instrucción CPU)
            A) se supone que cuando se agrega el PRC a EXIT desde corto plazo, ya se actualizó su ctx de ejecución en el momento en que CPU devolvió el PROCESO, así que sólo se liberarían recursos y memoria

        Todos estos casos se reducen a lo siguiente (evaluando acción según estado del proceso):
            0. en cualquier de los casos, lo que se hace antes de pasarlo a la cola EXIT, es sacarlo de la cola desde donde se encuentra actualmente!!!!
            I. RUNNING: cuando se ejecuta finalizar_proceso desde consola a un proceso que tiene estado running, sólo se manda la interrupción a CPU para que lo libere 
                    (desde corto plazo, una vez devuelto a kernel con su respectivo motivo de desalojo (ej: interrupcion_finalizar_proceso), se lo saca de la cola RUNNING, 
                    ¿se liberan recursos acá en caso de corresponder?, se lo agrega a la cola EXIT con ctx ya actualizado y se le deja el estado aún en RUNNING)
                    (analizar liberar los recursos, en caso de que corresponda, o ver si se puede hacer desde este hilo EXIT)
            ----- A PARTIR DE ACÁ: PARA TODOS LOS OTROS CASOS, SEA DESDE CONSOLA O DESDE CORTO PLAZO, SE AGREGA EL PRC DIRECTAMENTE A LA COLA EXIT, 
                    sacándolo primero de la cola desde donde se encuentra actualmente Y SE MANEJA EL PASO A PASO DESDE ESTE HILO -----
            1. NEW: se libera memoria, se pasa a estado EXIT
            2. READY: se aumenta un grado de multiprogramación, se libera memoria, se pasa a estado EXIT
            3. RUNNING: (ya realizado todo lo del caso I o por instrucción o error en momento de ejecución donde el proceso se lo devolvió a kernel y se realizaron los mismos pasos que en el caso I),
                        se libera memoria, se pasa a estado EXIT
            4. BLOCKED: se lo saca de la cola desde donde esté bloqueado (analizar si ese puntero a cola se guarda en el pcb), se libera memoria, se pasa a estado EXIT
            5. EXIT: no se hace nada, ya está en EXIT...     

            luego, se los saca de la cola EXIT
        */

        /*
        // TODO: analizar que pasa si se ejecutan dos finalizar procesos uno tras otro? analizar semaforo.
        if (FINALIZACION.FLAG_FINALIZACION)
        {
            if (obtener_cola(FINALIZACION.PID) == cola_RUNNING)
            {
                // mensaje de interrupt al cpu
                queue_push(cola_EXIT, queue_peek(cola_RUNNING));
                queue_pop(cola_RUNNING);
            }
            else
            {
                queue_push(cola_EXIT, queue_peek(obtener_cola(FINALIZACION.PID)));
                queue_pop(obtener_cola(FINALIZACION.PID));
            }
            if (obtener_cola(FINALIZACION.PID) != cola_NEW)
            {
                // habilitar espacio de multiprogramación (esto será muy probablemente un semáforo)
            }
            // poner error si se encuentra en la cola_EXIT
            FINALIZACION.FLAG_FINALIZACION = FALSE;
        }
        */
    }
    return NULL;
}

void crear_colas()
{
    cola_NEW = queue_create();
    cola_READY = queue_create();
    cola_BLOCKED = queue_create();
    cola_RUNNING = queue_create();
    cola_EXIT = queue_create();
}

void *buscar_pcb_por_pid(uint32_t pid_buscado)
{
    bool comparar_pid(void *elemento)
    {
        t_pcb *pcb = (t_pcb *)elemento;
        return pcb->pid == pid_buscado;
    }
    return list_find(pcb_list, comparar_pid); // si no funciona pasarlo como puntero a funcion
}

t_queue *obtener_cola(uint32_t pid_buscado)
{
    // t_pcb *pcb = dictionary_get(pcb_dictionary, pid_buscado);
    t_pcb *pcb_encontrado = (t_pcb *)buscar_pcb_por_pid(pid_buscado);
    switch (pcb_encontrado->estado)
    {
    case NEW:
        return cola_NEW;
    case READY:
        return cola_READY;
    case BLOCKED:
        return cola_BLOCKED;
    case RUNNING:
        return cola_RUNNING;
    case EXIT:
        return cola_EXIT;
    default:
        return 0; // y mensaje de error
    }
    return NULL;
}

void destruir_colas()
{ // ver si es mejor usar queue_destroy_and_destroy_elements o esto, es opcional el parametro ese??
    queue_clean(cola_NEW);
    queue_destroy(cola_NEW);
    queue_clean(cola_READY);
    queue_destroy(cola_READY);
    queue_clean(cola_BLOCKED);
    queue_destroy(cola_BLOCKED);
    queue_clean(cola_RUNNING);
    queue_destroy(cola_RUNNING);
    queue_clean(cola_EXIT);
    queue_destroy(cola_EXIT);
    free(cola_NEW);
    free(cola_READY);
    free(cola_BLOCKED);
    free(cola_RUNNING);
    free(cola_EXIT);
}

//////////////////////////////////////////////////////////////////////////////////////////////////////

// esto debería ir en memoria, y se ejecuta despues de verificar que el path existe.
uint32_t iniciar_proceso(void *arg)
{

    t_pcb *proceso = malloc(sizeof(t_pcb));

    proceso->estado = NEW;
    proceso->quantum = 0;
    proceso->program_counter = 0; // arranca en 0?
    proceso->pid = pid;
    // proceso->registros = obtener_registros(/*arg? registros cpu???*/);

    pid++;

    queue_push(cola_NEW, proceso);
    // dictionary_put(pcb_dictionary, pid, proceso);
    list_add(pcb_list, proceso);
    return proceso->pid;
}
/*
t_registros_cpu obtener_registros(){
    t_registros_cpu registros;
    registros.PC = &malloc(sizeof(uint32_t));
    registros.AX = &malloc(sizeof(uint8_t));
    registros.BX = &malloc(sizeof(uint8_t));
    registros.CX = &malloc(sizeof(uint8_t));
    registros.DX = &malloc(sizeof(uint8_t));
    registros.EAX = &malloc(sizeof(uint32_t));
    registros.EBX = &malloc(sizeof(uint32_t));
    registros.ECX = &malloc(sizeof(uint32_t));
    registros.EDX = &malloc(sizeof(uint32_t));
    registros.SI = &malloc(sizeof(uint32_t));
    registros.DI = &malloc(sizeof(uint32_t));
}
*/
/*


void finalizar_proceso(char* pid_buscado){

    struct {
        uint8_t _pid = (uint8_t)atoi(pid_buscado);
        t_pcb* elemento;
    }
    t_pcb* proc = list_find(pcb_list, comparacion);
    free(proc);

}
*/


void* atender_cliente(void* cliente){
	int cliente_recibido = *(int*) cliente;
	while(1){
		int cod_op = recibir_operacion(cliente_recibido); // bloqueante
		switch (cod_op)
		{
		case CONEXION:
			recibir_conexion(cliente_recibido); // TODO: acá KERNEL recibiría una conexion con una interfaz I/O 
			break;
		case PAQUETE:
			t_list* lista = recibir_paquete(cliente_recibido);
			log_info(logger, "Me llegaron los siguientes valores:\n");
			list_iterate(lista, (void*) iterator); //esto es un mapeo
			break;
		case -1:
			log_error(logger, "Cliente desconectado.");
			close(cliente_recibido); // cierro el socket accept del cliente
			free(cliente); // libero el malloc reservado para el cliente
			pthread_exit(NULL); // solo sale del hilo actual => deja de ejecutar la función atender_cliente que lo llamó
		default:
			log_warning(logger, "Operacion desconocida.");
			break;
		}
	}
}

//////////////////////////////////////////////////////////////////////////////////////////////////////