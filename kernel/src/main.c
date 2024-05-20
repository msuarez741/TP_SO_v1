#include <stdlib.h>
#include <stdio.h>
#include <utils/hello.h>
#include "main.h"
#include "monitores.h"

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
fc_puntero algoritmo_planificacion;

uint8_t PLANIFICACION_PAUSADA;
prcs_fin FINALIZACION;

/* se crean monitores en su lugar!
t_queue *cola_NEW;
t_queue *cola_READY;
t_queue *cola_BLOCKED;
t_queue *cola_RUNNING;
t_queue *cola_EXIT;
*/
t_queue *cola_READY_VRR;

sem_t mutex_planificacion_pausada, contador_grado_multiprogramacion, orden_planificacion, listo_proceso_en_running;
int conexion_cpu_dispatch, conexion_memoria, conexion_cpu_interrupt;

int main(int argc, char *argv[])
{
    // ------------ DECLARACION HILOS HIJOS PRINCIPALES ------------ //
    pthread_t thread_kernel_servidor, thread_kernel_consola, thread_planificador_corto_plazo, thread_planificador_largo_plazo;

    // ------------ ARCHIVOS CONFIGURACION + LOGGER ------------
    t_config *archivo_config = iniciar_config("kernel.config");
    cargar_config_struct_KERNEL(archivo_config);
    algoritmo_planificacion = obtener_algoritmo_planificacion();
    logger = log_create("log.log", "Servidor", 0, LOG_LEVEL_DEBUG);

    // -- INICIALIZACION VARIABLES GLOBALES -- //
    pid = 1;
    PLANIFICACION_PAUSADA = 0;
    pcb_list = list_create();
    crear_monitores(); // crear_colas();

    // -- INICIALIZACION SEMAFOROS -- //
    sem_init(&mutex_planificacion_pausada, 0, 1);
    sem_init(&orden_planificacion, 0, 0);
    sem_init(&contador_grado_multiprogramacion, 0, config_get_int_value(archivo_config, "GRADO_MULTIPROGRAMACION"));
    sem_init(&listo_proceso_en_running, 0, 0);

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

    destruir_monitores();

    sem_destroy(&mutex_planificacion_pausada);
    sem_destroy(&contador_grado_multiprogramacion);
    sem_destroy(&orden_planificacion);
    sem_destroy(&listo_proceso_en_running);

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
                    consola_interactiva(path);
                    printf("path ingresado (ejecutar_script): %s\n", path);
                }
            } // INICIAR_PROCESO /carpetaProcesos/proceso1
            else if (strcmp(comando, "INICIAR_PROCESO") == 0 && string_array_size(tokens) >= 2)
            {
                char *path = tokens[1];
                uint8_t pid_proceso_iniciado;
                t_paquete* paquete_proceso = crear_paquete();
                if (strlen(path) != 0 && path != NULL)
                {   
                    agregar_a_paquete(paquete_proceso, path, sizeof(path));
                    paquete_proceso->codigo_operacion = INICIAR_PROCESO;
                    enviar_paquete(paquete_proceso, conexion_memoria);
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

void consola_interactiva(char* ruta_archivo){
    /*char* instruccion = malloc(50);
    char datoLeido;
    FILE* script = fopen(ruta_archivo, "rb+");
    if (script == NULL){
        log_error(logger, "No se encontró ningún archivo con el nombre indicado...");
    } else {
        while (!feof(script))
        {
            fread(&datoLeido, sizeof(char), sizeof(datoLeido), script);
            if(datoLeido == "\n"){
                printf("INSTRUCCION LEIDA %s", instruccion);
                consola_kernel(instruccion);
            }
        }
    }*/
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
            // 1. acá dentro del mutex sólo se ejecutaría la función del algoritmo de planificación para sacar el proceso que corresponda de ready y pasarlo a RUNNING
            algoritmo_planificacion(); // ejecuta algorimo de planificacion corto plazo según valor del config
            sleep(3);
            log_info(logger, "estas en corto plazoo");
            sem_wait(&listo_proceso_en_running);
            // 2. se manda el proceso seleccionado a CPU a través del puerto 
            // supongamos que pasaron dos tiempos y se devolvio proceso: lo desalojas y ponele que lo mandas a cola bloqueado por I/O
            // 3. se aguarda respuesta de CPU para tomar una decisión y continuar con la ejecución de procesos 
            /*
            PUEDE PASAR QUE NECESITES BLOQUEAR EL PROCESO => ver si se tiene que delegar en otro hilo que haga el mediano plazo
            */
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
            if (!mqueue_is_empty(monitor_NEW)){
                sem_wait(&contador_grado_multiprogramacion);
                t_pcb* primer_elemento = mqueue_pop(monitor_NEW);
                primer_elemento->estado = READY;
                mqueue_push(monitor_READY, primer_elemento);
                // TODO: Log cambio de estado
                // TODO: ingreso a READYs
                log_info(logger, "estas en largo plazoo");
                sem_post(&orden_planificacion);
            }
        }
        else
            sem_post(&mutex_planificacion_pausada);
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

//////////////////////////////////////////////////////////////////////////////////////////////////////

// esto debería ir en memoria, y se ejecuta despues de verificar que el path existe.
uint32_t iniciar_proceso(char *path){

    t_pcb *proceso = malloc(sizeof(t_pcb));

    proceso->estado = NEW;
    proceso->quantum = config.quantum;
    proceso->program_counter = 0; // arranca en 0?
    proceso->pid = pid;
    // proceso->registros = obtener_registros(/*arg? registros cpu???*/);

    mqueue_push(monitor_NEW, proceso);

    char* nuevo_proceso = (char*)malloc(128);
    sprintf(nuevo_proceso, "Se crea el proceso %d en NEW", proceso->pid);
    log_info(logger, "%s", nuevo_proceso);
    free(nuevo_proceso);

    pid++;

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


// ALGORITMOS DE PLANIFICACION
fc_puntero obtener_algoritmo_planificacion(){
    if(strcmp(config.algoritmo_planificacion, "FIFO") == 0)
        return &algortimo_fifo;
    if(strcmp(config.algoritmo_planificacion, "RR") == 0)
        return &algoritmo_rr;
    if(strcmp(config.algoritmo_planificacion, "VRR") == 0){
        cola_READY_VRR = queue_create();
        return &algoritmo_vrr;
        
    }
    return NULL;
}

// cada algoritmo carga en (la cola?) running el prox. proceso a ejecutar, sacándolo de ready
void algortimo_fifo() {
    log_info(logger, "estas en fifo");

    t_pcb* primer_elemento = mqueue_pop(monitor_READY);
    primer_elemento->estado = RUNNING; 

    // --------- TODO: DELEGAR ESTO EN UNA FUNCION PARA LOGS PORQUE SE LLAMA A ESTE LOG DESDE DISTINTAS PARTES DEL CÓDIGO
    char* cambio_estado = (char*)malloc(128);
    sprintf(cambio_estado, "PID: %d - Estado Anterior: READY - Estado Actual: RUNNING", primer_elemento->pid);
    log_info(logger, "%s", cambio_estado);
    // --------- TODO: DELEGAR ESTO EN UNA FUNCION PARA LOGS PORQUE SE LLAMA A ESTE LOG DESDE DISTINTAS PARTES DEL CÓDIGO

    mqueue_push(monitor_RUNNING, primer_elemento);
    sem_post(&listo_proceso_en_running); // manda señal al planificador corto plazo para que envie proceso a CPU a traves del puerto dispatch
    sem_post(&contador_grado_multiprogramacion); // libera un espacio en READY

    //free(primer_elemento);
}

// los algoritmos RR van a levantar un hilo que se encargará de, terminado el quantum, mandar a la cpu una interrupción para desalojar el proceso
void algoritmo_rr(){
    log_info(logger, "estas en rr");
    pthread_t hilo_RR;
    char* algoritmo = "RR";
    pthread_create(&hilo_RR, NULL, control_quantum, algoritmo);
    pthread_detach(hilo_RR);

}
void algoritmo_vrr(){
    log_info(logger, "estas en vrr");
    pthread_t hilo_RR; 
    char* algoritmo = "VRR";
    pthread_create(&hilo_RR, NULL, control_quantum, algoritmo);
    pthread_detach(hilo_RR);

}

void* control_quantum(void* tipo_algoritmo){
        int duracion_quantum = config.quantum;
        t_pcb* primer_elemento;
        int quantum_por_ciclo;
        // Pregunta si es VRR y si hay algún proceso en la cola de pendientes de VRR (con quantum menor).
        if(strcmp((char*)tipo_algoritmo, "VRR") == 0 && !queue_is_empty(cola_READY_VRR)){
            primer_elemento = queue_pop(cola_READY_VRR); // solo la modificamos desde aca!!
            quantum_por_ciclo = primer_elemento->quantum; // toma el quantum restante de su PCB
        } else {
            primer_elemento = mqueue_pop(monitor_READY);
            quantum_por_ciclo = duracion_quantum / 1000; 
        }

        mqueue_push(monitor_RUNNING, primer_elemento); //TODO: se supone que se modifica de a uno y que no necesita SEMÁFOROS pero por el momento lo dejamos con el monitor (no debería ser una cola!!)
        sem_post(&listo_proceso_en_running); // manda señal al planificador corto plazo para que envie proceso a CPU a traves del puerto dispatch
        sem_post(&contador_grado_multiprogramacion); // libera un espacio en READY

        // 1. verificas que el proceso siga en RUNNNIG dentro del QUANTUM establecido => de lo contrario, matas el hilo
        // 2. si se acabo el quantum, manda INTERRUPCION A CPU        
        for(int i = quantum_por_ciclo; i<=0; i--){ 
            sleep(1);
            // TODO: EVALUAR SI ESTO DE ACA NO CORRESPONDERIA HACERLO CUANDO SE DEVUELVE EL PROCESO DE CPU con mensaje de desalojo distinto de quantum, asi no queda este hilo suelto dando lugar a errores de sincronizacion y la posibilidad de que mas de un hilo que ejecute control_quantum modifique las mismas variables simultaneamente
            if(primer_elemento->estado != RUNNING){ // si ya no esta en running (se desalojó el proceso por otro motivo que NO es FIN DE QUANTUM)
                if(strcmp(tipo_algoritmo, "VRR") == 0){
                 /* A.VRR)
                    1. cargar el quantum restante (i) en el PCB del proceso
                    2. mandar el proceso a la cola READY_VRR
                   */
                }
                pthread_exit(NULL); // solo sale del hilo actual => deja de ejecutar la funcion que lo llamo   
            }
        }
        // TODO: si salis del for, es porque pasaron 3 tiempos de KERNEL! y todavia el proceso esta running => necesita ser desalojado por QUANTUM
        // aca recien se manda INTERRUPCION A CPU!!!!! : cuando devuelva cpu a kernel, el planificador de mediano plazo (dentro de corot plazo) devuelve PRC a READY ante mensaje de desalojo FIN de QUANTUM
    return NULL;
}
//////////////////////////////////////////////////////////////////////////////////////////////////////