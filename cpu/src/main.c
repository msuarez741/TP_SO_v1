#include <stdlib.h>
#include <stdio.h>
#include <utils/hello.h>
#include <pthread.h>
#include <sys/time.h>
#include "main.h"
#include "instrucciones.h"


t_list* tlb_list;
int TAM_MEMORIA;
int TAM_PAGINA;
int TLB_HABILITADA;

config_struct config;
t_registros_cpu registros; 
int conexion_memoria; // SOCKET
int seguir_ejecucion = 1, desalojo = 0; //FLAGS
uint32_t pid_proceso; // PID PROCESO EN EJECUCIÓN
t_list* list_interrupciones;
sem_t mutex_lista_interrupciones;

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "NO hay suficientes parametros para %s\n", argv[0]);
        return EXIT_FAILURE;
    }

    // ------------ ARCHIVOS CONFIGURACION + LOGGER ------------
    pthread_t thread_interrupt; //, thread_dispatch;
    t_config *puertos_config = iniciar_config(argv[1]);
    t_config *archivo_config = iniciar_config(argv[2]);    
    cargar_config_struct_CPU(puertos_config, archivo_config);
    logger = log_create("cpu.log", "CPU", 1, LOG_LEVEL_DEBUG);

    tlb_list = list_create();
    TLB_HABILITADA = (config.cantidad_entradas_tlb == 0) ? 0 : 1;
    list_interrupciones = list_create();

    sem_init(&mutex_lista_interrupciones, 0, 1);

    decir_hola("CPU");

    // ------------ CONEXION CLIENTE - SERVIDORES ------------
    // conexion memoria
    conexion_memoria = crear_conexion(config.ip_memoria, config.puerto_memoria);
    enviar_conexion("CPU", conexion_memoria);

    // le pido los valores de TAM_MEMORIA y TAM_PAGINA
    int operacion = DATOS_MEMORIA;
    send(conexion_memoria, &operacion, sizeof(operacion), 0);
    recibir_operacion(conexion_memoria); // espera a recibir los datos desde memoria
    t_sbuffer *buffer_memoria = cargar_buffer(conexion_memoria);
    TAM_MEMORIA = buffer_read_int(buffer_memoria);
    TAM_PAGINA  = buffer_read_int(buffer_memoria);
    buffer_destroy(buffer_memoria);
    //log_debug(logger, "obtengo valores de tam_memoria %d y de tam_pagina %d", TAM_MEMORIA, TAM_PAGINA);

    // ------------ CONEXION SERVIDOR - CLIENTES ------------
    // conexion dispatch
    int socket_servidor_dispatch = iniciar_servidor(config.puerto_escucha_dispatch);
    inicializar_registros();
    log_info(logger, "Server CPU DISPATCH, puerto %s", config.puerto_escucha_dispatch);

    // conexion interrupt
    int socket_servidor_interrupt = iniciar_servidor(config.puerto_escucha_interrupt);
    log_info(logger, "Server CPU INTERRUPT, puerto %s", config.puerto_escucha_interrupt); 

    // hilo que recibe las interrupciones y las guarda en una 'lista' de interrupciones (PIC: Programmable Integrated Circuited) 
    pthread_create(&thread_interrupt, NULL, recibir_interrupcion, &socket_servidor_interrupt); 
    pthread_detach(thread_interrupt);

    // NO hace falta un hilo para DISPATCH porque sólo KERNEL manda solicitudes a CPU, de forma SECUENCIAL (GRADO MULTIPROCESAMIENTO = 1)
    int cliente_kernel = esperar_cliente(socket_servidor_dispatch); 
    int cod_op;
    while((cod_op = recibir_operacion(cliente_kernel)) != -1){
        // DESDE ACA SE MANEJAN EJECUCIONES DE PROCESOS A DEMANDA DE KERNEL 
        switch (cod_op){
        case CONEXION:
            recibir_conexion(cliente_kernel);
            break;
        case EJECUTAR_PROCESO:
            seguir_ejecucion = 1;
            desalojo = 0;
            
            // guarda BUFFER del paquete enviado
            t_sbuffer *buffer_dispatch = cargar_buffer(cliente_kernel);
            
            // guarda datos del buffer (contexto de proceso)
            pid_proceso = buffer_read_uint32(buffer_dispatch); // guardo PID del proceso que se va a ejecutar
            buffer_read_registros(buffer_dispatch, &(registros)); // cargo contexto del proceso en los registros de la CPU

            // comienzo ciclo instrucciones
            while(seguir_ejecucion){
                ciclo_instruccion(cliente_kernel); // supongo que lo busca con PID en memoria (ver si hay que pasar la PATH en realidad, y si ese dato debería ir en el buffer y en el pcb de kernel)
            }
            buffer_destroy(buffer_dispatch);
            break;

        case -1:
            log_error(logger, "cliente desconectado");
            break;
        default:
            log_warning(logger, "Operacion desconocida.");
            break;
        }
    }

    //Limpieza
    pthread_cancel(thread_interrupt);
    log_destroy(logger);
    list_destroy(tlb_list);
	config_destroy(archivo_config);
    config_destroy(puertos_config);
    liberar_conexion(conexion_memoria);

    return 0;
}

void cargar_config_struct_CPU(t_config* puertos_config, t_config* archivo_config){
    config.ip_memoria = config_get_string_value(puertos_config, "IP_MEMORIA");
    config.puerto_memoria = config_get_string_value(puertos_config, "PUERTO_MEMORIA");
    config.puerto_escucha_dispatch = config_get_string_value(puertos_config, "PUERTO_ESCUCHA_DISPATCH");
    config.puerto_escucha_interrupt = config_get_string_value(puertos_config, "PUERTO_ESCUCHA_INTERRUPT");
    config.cantidad_entradas_tlb = config_get_int_value(archivo_config, "CANTIDAD_ENTRADAS_TLB");
    config.algoritmo_tlb = config_get_string_value(archivo_config, "ALGORITMO_TLB");
}

void inicializar_registros(){
    registros.PC = 0;
    registros.AX = 0;
    registros.BX = 0;
    registros.CX = 0;
    registros.DX = 0;
    registros.EAX = 0;
    registros.EBX = 0;
    registros.ECX = 0;
    registros.EDX = 0;
    registros.SI = 0;
    registros.DI = 0;
}

void ciclo_instruccion(int conexion_kernel){
    // ---------------------- ETAPA FETCH ---------------------- //
    // 1. FETCH: búsqueda de la sgte instrucción en MEMORIA (por valor del Program Counter pasado por el socket)
    log_info(logger, "PID: %u - FETCH - Program Counter: %u", pid_proceso, registros.PC); 
    // provisoriamente, le pasamos PID y PC a memoria, con codigo de operación LEER_PROCESO
    t_sbuffer *buffer = buffer_create(
        sizeof(uint32_t) * 2 // PID + PC
    );

    buffer_add_uint32(buffer,pid_proceso);
    buffer_add_uint32(buffer, registros.PC);
    
    cargar_paquete(conexion_memoria, LEER_PROCESO, buffer); 

    if(recibir_operacion(conexion_memoria) == INSTRUCCION){
        t_sbuffer *buffer_de_instruccion = cargar_buffer(conexion_memoria);
        uint32_t length;
        char* leido = buffer_read_string(buffer_de_instruccion, &length);
        leido[strcspn(leido, "\n")] = '\0'; // CORREGIR: DEBE SER UN PROBLEMA DESDE EL ENVÍO DEL BUFFER!
	log_info(logger, "PID: %u - Ejecutando %s", pid_proceso, leido);

        // ---------------------- ETAPA DECODE + EXECUTE  ---------------------- //
        // 2. DECODE: interpretar qué instrucción es la que se va a ejecutar y si la misma requiere de una traducción de dirección lógica a dirección física.
        ejecutar_instruccion(leido, conexion_kernel); 
        
        // ---------------------- CHECK INTERRUPT  ---------------------- //
        // 4. CHECK INTERRUPT: chequea interrupciones en PIC y las maneja (cola de interrupciones y las atiende por prioridad/orden ANALIZAR) 
        check_interrupt(pid_proceso, conexion_kernel);
      
        // IMPORTANTE: si por algun motivo se va a la mrd (INT, EXIT O INST como WAIT (ver si deja de ejecutar)): seguir_ejecutando = 0 + desalojo = 1;

        // libero recursos
        free(leido);
        buffer_destroy(buffer_de_instruccion);
    } else {
        // TODO: PROBLEMAS
    }
}

bool coinciden_pid(void* element, uint32_t pid){
    return ((t_pic*)element)->pid == pid;
}

bool comparar_prioridad(void* a, void* b) {
    t_pic* pic_a = (t_pic*)a;
    t_pic* pic_b = (t_pic*)b;
    return pic_a->bit_prioridad < pic_b->bit_prioridad;
}

t_list* filtrar_y_remover_lista(t_list* lista_original, bool(*condicion)(void*, uint32_t), uint32_t pid){
    t_list* lista_filtrada = list_create();
    sem_wait(&mutex_lista_interrupciones);
    t_list_iterator* iterator = list_iterator_create(lista_original);

    while (list_iterator_has_next(iterator)) {
        // Obtener el siguiente elemento de la lista original
        void* element = list_iterator_next(iterator);

        // Verificar si el elemento cumple con la condición
        if (condicion(element, pid)) {
            // Agregar el elemento a la lista filtrada
            list_add(lista_filtrada, element);
            // Remover el elemento de la lista original sin destruirlo
            list_iterator_remove(iterator);
        }
    }
    sem_post(&mutex_lista_interrupciones);

    list_iterator_destroy(iterator);
    return lista_filtrada;
}

void check_interrupt(uint32_t proceso_pid, int conexion_kernel){
    sem_wait(&mutex_lista_interrupciones);
    if(!list_is_empty(list_interrupciones)){
        sem_post(&mutex_lista_interrupciones);
        // remueve las interrupciones del proceso actual aun si el proceso ya fue desalojado => para que no se traten en la prox. ejecucion (si es que vuelve a ejecutar)
        t_list* interrupciones_proceso_actual = filtrar_y_remover_lista(list_interrupciones, coinciden_pid, proceso_pid);
        if(!list_is_empty(interrupciones_proceso_actual) && desalojo == 0){ // procesamos la interrupcion si todavia no se desalojo la ejecucion del proceso durante el proceso de EJECUCION
            list_sort(interrupciones_proceso_actual, comparar_prioridad);
            // solo proceso la interrupcion de mas prioridad, que sera la primera!
            seguir_ejecucion = 0;
            t_pic* primera_interrupcion = (t_pic*)list_get(interrupciones_proceso_actual, 0);
            t_sbuffer *buffer_desalojo_interrupt = NULL;
            desalojo_proceso(&buffer_desalojo_interrupt, conexion_kernel, primera_interrupcion->motivo_interrupcion);
	    log_info(logger, "Desaloje el proceso %u, por %s", primera_interrupcion->pid, (primera_interrupcion->motivo_interrupcion == 12) ? "FIN QUANTUM" : "FINALIZAR PROCESO");
            //log_debug(logger, "Desaloje el proceso %u, por INT %d", primera_interrupcion->pid, primera_interrupcion->motivo_interrupcion);
        }
        list_clean_and_destroy_elements(interrupciones_proceso_actual, free); // Libera cada elemento en la lista filtrada
        list_destroy(interrupciones_proceso_actual);
    } else 
        sem_post(&mutex_lista_interrupciones);
}

void ejecutar_instruccion(char* leido, int conexion_kernel) {
    // 2. DECODE: interpretar qué instrucción es la que se va a ejecutar y si la misma requiere de una traducción de dirección lógica a dirección física.

    char **tokens = string_split(leido, " ");
    char *comando = tokens[0];
    // ------------------------------- ETAPA EXECUTE ---------------------------------- //
    // 3. EXECUTE: ejecutar la instrucción, actualizando su contexto.  Considerar que ciertas INSTRUCCIONES deben mandar por puerto DISPATCH a KERNEL ciertas solicitudes, como por ejemplo, WAIT-SIGNAL-ACCEDER A I/O-EXIT ..., y deberán esperar la respuesta de KERNEL para seguir avanzando
    if (comando != NULL){
        registros.PC++; // Se suma 1 al Program Counter (por si desaloja para que desaloje con el PC actualizado!)

        if (strcmp(comando, "SET") == 0){
            char *parametro1 = tokens[1]; 
            char *parametro2 = tokens[2]; 
            set(parametro1, parametro2);
        } else 
        if (strcmp(comando, "SUM") == 0){
            char *parametro1 = tokens[1]; 
            char *parametro2 = tokens[2]; 
            SUM(parametro1, parametro2);
        } else if (strcmp(comando, "SUB") == 0){
            char *parametro1 = tokens[1]; 
            char *parametro2 = tokens[2]; 
            SUB(parametro1, parametro2);
        } else if (strcmp(comando, "JNZ") == 0){
            char *registro = tokens[1]; 
            char *proxInstruccion = tokens[2]; 
            jnz(registro, proxInstruccion);
        } else if (strcmp(comando, "EXIT") == 0){
            seguir_ejecucion = 0;
            desalojo = 1; // EN TODAS LAS INT donde se DESALOJA EL PROCESO cargar 1 en esta variable!!
            t_sbuffer *buffer_desalojo = NULL;
            desalojo_proceso(&buffer_desalojo, conexion_kernel, EXIT_PROCESO);
        } else if (strcmp(comando, "RESIZE") == 0){
            int tamanio_en_bytes = atoi(tokens[1]); 
            t_sbuffer* buffer_memoria_resize = buffer_create(
                sizeof(uint32_t) + // pid 
                sizeof(int) // resize
            );
            buffer_add_uint32(buffer_memoria_resize, pid_proceso);
            buffer_add_int(buffer_memoria_resize, tamanio_en_bytes);
            cargar_paquete(conexion_memoria, RESIZE, buffer_memoria_resize);
            int respuesta_resize_memoria = recibir_operacion(conexion_memoria);
            switch (respuesta_resize_memoria){
            case OUT_OF_MEMORY:
                seguir_ejecucion = 0;
                desalojo = 1;
                t_sbuffer *buffer_out_of_memory = NULL;
                desalojo_proceso(&buffer_out_of_memory, conexion_kernel, OUT_OF_MEMORY);
                break;
            default:
                break;
            }
        } else if (strcmp(comando, "MOV_OUT") == 0){
            char* registro_direccion = tokens[1];
            char* registro_dato = tokens[2]; // donde toma el dato para escribir
            uint32_t direccion_logica = obtener_valor_registro(registro_direccion);
            uint32_t bytes_a_escribir = (obtenerTipo(registro_dato) == _UINT8) ? 1 : 4; // 1 byte si es uint8 y 4 bytes si es uint32
            uint32_t valor_a_escribir = obtener_valor_registro(registro_dato);

            // mando a MMU para que calcule dir. física y me retorne un buffer ya cargado con estos valores!
            t_sbuffer* buffer_mmu = mmu("ESCRIBIR", direccion_logica, bytes_a_escribir, &valor_a_escribir);
            
            cargar_paquete(conexion_memoria, PETICION_ESCRITURA, buffer_mmu);
            recibir_operacion(conexion_memoria); // espera respuesta de memoria
            /* analizar si le puede mandar algún error
            switch (recibir_operacion_memoria){
            case ERROR_MEMORIA: 
                seguir_ejecucion = 0;
                desalojo = 1;
                break;
            default:
                break;
            }
            */
        } else if (strcmp(comando, "MOV_IN") == 0){
            char* registro_dato = tokens[1]; // donde va a almacenar el valor que lee de memoria
            char* registro_direccion = tokens[2];
            uint32_t direccion_logica = obtener_valor_registro(registro_direccion);
            uint32_t bytes_a_leer = (obtenerTipo(registro_dato) == _UINT8) ? 1 : 4; // 1 byte si es uint8 y 4 bytes si es uint32

            // mando a MMU para que calcule dir. física y me retorne un buffer ya cargado con estos valores!
            t_sbuffer* buffer_mmu = mmu("LEER", direccion_logica, bytes_a_leer, &bytes_a_leer);
            
            cargar_paquete(conexion_memoria, PETICION_LECTURA, buffer_mmu);
            int recibir_operacion_memoria = recibir_operacion(conexion_memoria); // espera respuesta de memoria
            switch (recibir_operacion_memoria){
                case PETICION_LECTURA: 
                    t_sbuffer* buffer_rta_peticion_lectura = cargar_buffer(conexion_memoria);
                    int cantidad_peticiones = buffer_read_int(buffer_rta_peticion_lectura);
                    
                    void* peticion_completa = malloc(bytes_a_leer);
                    uint32_t bytes_recibidos = 0;
                    for (size_t i = 0; i < cantidad_peticiones; i++){
                        uint32_t bytes_peticion;
                        void* dato_peticion = buffer_read_void(buffer_rta_peticion_lectura, &bytes_peticion);
                        memcpy(peticion_completa + bytes_recibidos, dato_peticion, bytes_peticion);
                        bytes_recibidos += bytes_peticion;
                        free(dato_peticion);
                    }

                    // se asigna el valor al registro que corresponda
                    char valor_leido_str[16];
                    if (bytes_a_leer == 1) {
                        uint8_t valor_leido;
                        memcpy(&valor_leido, peticion_completa, bytes_a_leer);
                        snprintf(valor_leido_str, sizeof(valor_leido_str), "%u", valor_leido);
                    } else {
                        uint32_t valor_leido;
                        memcpy(&valor_leido, peticion_completa, bytes_a_leer);
                        snprintf(valor_leido_str, sizeof(valor_leido_str), "%u", valor_leido);
                    }

                    set(registro_dato, valor_leido_str);
                    free(peticion_completa);
                    buffer_destroy(buffer_rta_peticion_lectura);
                break;
            }
        } else if (strcmp(comando, "COPY_STRING") == 0){
            uint32_t tamanio_a_leer = atoi(tokens[1]);
            uint32_t direccion_logica_lectura = obtener_valor_registro("SI");
            uint32_t direccion_logica_escritura = obtener_valor_registro("DI");
            t_sbuffer* buffer_mmu_lectura = mmu("LEER", direccion_logica_lectura, tamanio_a_leer, &tamanio_a_leer);
            
            cargar_paquete(conexion_memoria, PETICION_LECTURA, buffer_mmu_lectura);
            int recibir_operacion_memoria = recibir_operacion(conexion_memoria); // espera respuesta de memoria
            switch (recibir_operacion_memoria){
                case PETICION_LECTURA: 
                    t_sbuffer* buffer_rta_peticion_lectura = cargar_buffer(conexion_memoria);
                    int cantidad_peticiones_lectura = buffer_read_int(buffer_rta_peticion_lectura);
                    
                    void* peticion_completa = malloc(tamanio_a_leer);
                    uint32_t bytes_recibidos = 0;
                    for (size_t i = 0; i < cantidad_peticiones_lectura; i++){
                        uint32_t bytes_peticion;
                        void* dato_peticion = buffer_read_void(buffer_rta_peticion_lectura, &bytes_peticion);
                        memcpy(peticion_completa + bytes_recibidos, dato_peticion, bytes_peticion);
                        bytes_recibidos += bytes_peticion;
                        free(dato_peticion);
                    }

                    t_sbuffer* buffer_mmu_escritura = mmu("ESCRIBIR", direccion_logica_escritura, tamanio_a_leer, peticion_completa);
                    cargar_paquete(conexion_memoria, PETICION_ESCRITURA, buffer_mmu_escritura);
                    recibir_operacion(conexion_memoria); // espera que cargue valor en memoria

                    free(peticion_completa);
                    buffer_destroy(buffer_rta_peticion_lectura);
                break;
            }
        } else if (strcmp(comando, "WAIT") == 0 || strcmp(comando, "SIGNAL") == 0){
            char *recurso = tokens[1];
            op_code instruccion_recurso = (strcmp(comando, "WAIT") == 0) ? WAIT_RECURSO : SIGNAL_RECURSO;
            t_sbuffer *buffer_desalojo_wait = buffer_create(
                (uint32_t)strlen(recurso) + sizeof(uint32_t) + //la longitud del string
                sizeof(uint32_t) * 8 // REGISTROS: PC, EAX, EBX, ECX, EDX, SI, DI
                + sizeof(uint8_t) * 4 // REGISTROS: AX, BX, CX, DX
            );
            buffer_add_string(buffer_desalojo_wait, (uint32_t)strlen(recurso), recurso);
            desalojo_proceso(&buffer_desalojo_wait, conexion_kernel, instruccion_recurso); // agrega ctx en el buffer y envía paquete a kernel

            int respuesta = recibir_operacion(conexion_kernel); // BLOQUEANTE: espera respuesta de kernel
            switch (respuesta){
            case DESALOJAR:
                seguir_ejecucion = 0;
                desalojo = 1; // EN TODAS LAS INT donde se DESALOJA EL PROCESO cargar 1 en esta variable!!
                break;
            default:
                // en caso de que la respuesta sea CONTINUAR, se continúa ejecutando normalmente
                break;
            }
        }
        ///////////////////////////// INSTRUCCIONES DE IO ///////////////////////////// 
        else if (strcmp(comando, "IO_GEN_SLEEP") == 0){
            char* nombre_interfaz = tokens[1];
            uint32_t tiempo_sleep = (uint32_t)atoi(tokens[2]);
            //io_gen_sleep(nombre_interfaz, tiempo_sleep);
            t_sbuffer *buffer_interfaz_gen_sleep = buffer_create(
                (uint32_t)strlen(nombre_interfaz) + sizeof(uint32_t) +
                sizeof(uint32_t) +
                sizeof(uint32_t) * 8 // REGISTROS: PC, EAX, EBX, ECX, EDX, SI, DI
                + sizeof(uint8_t) * 4 // REGISTROS: AX, BX, CX, DX
            );

            buffer_add_string(buffer_interfaz_gen_sleep, (uint32_t)strlen(nombre_interfaz), nombre_interfaz);
            buffer_add_uint32(buffer_interfaz_gen_sleep, tiempo_sleep);
            desalojo_proceso(&buffer_interfaz_gen_sleep, conexion_kernel, IO_GEN_SLEEP); // agrega ctx en el buffer y envía paquete a kernel

            // EN INST. IO no hace falta esperar respuesta de KERNEL ya que siempre se bloquea o se manda a exit en caso de error!
            seguir_ejecucion = 0;
            desalojo = 1; 
            
        } else if (strcmp(comando, "IO_STDIN_READ") == 0 || strcmp(comando, "IO_STDOUT_WRITE") == 0){
            char* nombre_interfaz = tokens[1];
            char* registro_direccion = tokens[2];
            char* registro_tamanio = tokens[3];
            uint32_t direccion_logica = obtener_valor_registro(registro_direccion);
            uint32_t tamanio = obtener_valor_registro(registro_tamanio); 

            // indico a MMU LEER para que sólo cargue las direcciones físicas sobre las cuales tiene que escribir/leer el dato leído por consola/pedido por IO.
            t_sbuffer* buffer_mmu = mmu("LEER", direccion_logica, tamanio, &tamanio);
            
            t_sbuffer *buffer_interfaz_stdin_stdout = buffer_create(
                strlen(nombre_interfaz) + sizeof(uint32_t) // nombre interfaz
                + buffer_mmu->size + sizeof(uint32_t) // tamanio del buffer con direcciones fisicas y bytes por peticion + uint32_t para guardar el tamanio
                + sizeof(uint32_t) // VALOR REGISTRO TAMANIO
                + sizeof(uint32_t) * 7 // REGISTROS: PC, EAX, EBX, ECX, EDX, SI, DI
                + sizeof(uint8_t) * 4 // REGISTROS: AX, BX, CX, DX
            );

            buffer_add_string(buffer_interfaz_stdin_stdout, (uint32_t)strlen(nombre_interfaz), nombre_interfaz); // nombre interfaz
            buffer_add_registros(buffer_interfaz_stdin_stdout, &(registros)); // contexto para que actualice antes de bloquearse por interfaz
            buffer_add_uint32(buffer_interfaz_stdin_stdout, tamanio);
            buffer_add_buffer(buffer_interfaz_stdin_stdout, buffer_mmu); // lo carga y libera memoria

            op_code mensaje_desalojo = (strcmp(comando, "IO_STDIN_READ") == 0) ? IO_STDIN_READ : IO_STDOUT_WRITE;
            cargar_paquete(conexion_kernel, mensaje_desalojo, buffer_interfaz_stdin_stdout); // desaloja CPU y libera buffer creado

            // EN INST. IO no hace falta esperar respuesta de KERNEL ya que siempre se bloquea o se manda a exit en caso de error!
            seguir_ejecucion = 0;
            desalojo = 1; 
            
        } else if (strcmp(comando, "IO_FS_CREATE") == 0 || strcmp(comando, "IO_FS_DELETE") == 0){
            char* nombre_interfaz = tokens[1];
            char* nombre_file = tokens[2];
            t_sbuffer *buffer_interfaz_fs_create_delete = buffer_create(
                (uint32_t)strlen(nombre_interfaz) + sizeof(uint32_t)
                + (uint32_t)strlen(nombre_file) + sizeof(uint32_t)  // NOMBRE DE ARCHIVO
                + sizeof(uint32_t) * 7 // REGISTROS: PC, EAX, EBX, ECX, EDX, SI, DI
                + sizeof(uint8_t) * 4 // REGISTROS: AX, BX, CX, DX
            );

            buffer_add_string(buffer_interfaz_fs_create_delete, (uint32_t)strlen(nombre_interfaz), nombre_interfaz);
            buffer_add_string(buffer_interfaz_fs_create_delete, (uint32_t)strlen(nombre_file), nombre_file);
            op_code mensaje_desalojo = (strcmp(comando, "IO_FS_CREATE") == 0) ? IO_FS_CREATE : IO_FS_DELETE;
            desalojo_proceso(&buffer_interfaz_fs_create_delete, conexion_kernel, mensaje_desalojo); // agrega ctx en el buffer y envía paquete a kernel

            // EN INST. IO no hace falta esperar respuesta de KERNEL ya que siempre se bloquea o se manda a exit en caso de error!
            seguir_ejecucion = 0;
            desalojo = 1; 

        } else if (strcmp(comando, "IO_FS_TRUNCATE") == 0){
            char* nombre_interfaz = tokens[1];
            char* nombre_file = tokens[2];
            uint32_t reg_t = obtener_valor_registro(tokens[3]);

            t_sbuffer *buffer_interfaz_fs_truncate = buffer_create(
                (uint32_t)strlen(nombre_interfaz) + sizeof(uint32_t)
                + (uint32_t)strlen(nombre_file) + sizeof(uint32_t)  // NOMBRE DE ARCHIVO
                + sizeof(uint32_t) // REGISTRO PARA IO
                + sizeof(uint32_t) * 7 // REGISTROS: PC, EAX, EBX, ECX, EDX, SI, DI
                + sizeof(uint8_t) * 4 // REGISTROS: AX, BX, CX, DX
            );

            buffer_add_string(buffer_interfaz_fs_truncate, (uint32_t)strlen(nombre_interfaz), nombre_interfaz);
            buffer_add_string(buffer_interfaz_fs_truncate, (uint32_t)strlen(nombre_file), nombre_file);
            buffer_add_uint32(buffer_interfaz_fs_truncate, reg_t);
            desalojo_proceso(&buffer_interfaz_fs_truncate, conexion_kernel, IO_FS_TRUNCATE); // agrega ctx en el buffer y envía paquete a kernel

            // EN INST. IO no hace falta esperar respuesta de KERNEL ya que siempre se bloquea o se manda a exit en caso de error!
            seguir_ejecucion = 0;
            desalojo = 1; 
            
        } else if (strcmp(comando, "IO_FS_WRITE") == 0 || strcmp(comando, "IO_FS_READ") == 0){
            char* nombre_interfaz = tokens[1];
            char* nombre_file = tokens[2];
            uint32_t reg_d = obtener_valor_registro(tokens[3]);
            uint32_t reg_t = obtener_valor_registro(tokens[4]);
            uint32_t reg_p = obtener_valor_registro(tokens[5]); 

            // indico a MMU LEER para que solo cargue las direcciones físicas sobre las cuales tiene que escribir/leer el dato leído por consola/pedido por IO.
            t_sbuffer* buffer_mmu = mmu("LEER", reg_d, reg_t, &reg_t);

            t_sbuffer *buffer_interfaz_fs_write_read = buffer_create(
                strlen(nombre_interfaz) + sizeof(uint32_t) // Nombre interfaz
                + strlen(nombre_file) + sizeof(uint32_t)  // NOMBRE DE ARCHIVO
                + sizeof(uint32_t) * 2 // REGISTRO PARA IO (TAMANIO + PUNTERO)
                + sizeof(uint32_t) * 7 // REGISTROS: PC, EAX, EBX, ECX, EDX, SI, DI
                + sizeof(uint8_t) * 4 // REGISTROS: AX, BX, CX, DX
                + buffer_mmu->size + sizeof(uint32_t)  // tamanio del buffer con direcciones fisicas y bytes por peticion + uint32_t para guardar el tamanio
            );

            buffer_add_string(buffer_interfaz_fs_write_read, (uint32_t)strlen(nombre_interfaz), nombre_interfaz);
            buffer_add_registros(buffer_interfaz_fs_write_read, &(registros)); // contexto para que actualice antes de bloquearse por interfaz
            buffer_add_string(buffer_interfaz_fs_write_read, (uint32_t)strlen(nombre_file), nombre_file);
            buffer_add_uint32(buffer_interfaz_fs_write_read, reg_t);
            buffer_add_uint32(buffer_interfaz_fs_write_read, reg_p);
            buffer_add_buffer(buffer_interfaz_fs_write_read, buffer_mmu);

            op_code mensaje_desalojo = (strcmp(comando, "IO_FS_WRITE") == 0) ? IO_FS_WRITE : IO_FS_READ;
            cargar_paquete(conexion_kernel, mensaje_desalojo, buffer_interfaz_fs_write_read); // desaloja CPU y libera buffer creado

            // EN INST. IO no hace falta esperar respuesta de KERNEL ya que siempre se bloquea o se manda a exit en caso de error!
            seguir_ejecucion = 0;
            desalojo = 1; 
            
        } // TODO: SEGUIR
    }
    string_array_destroy(tokens);
}

// solo carga el contexto y retorna proceso (si tuvo que cargar otro valor antes suponemos que ya venía cargado en el buffer que se pasa por parámetro)
void desalojo_proceso(t_sbuffer **buffer_contexto_proceso, int conexion_kernel, op_code mensaje_desalojo){
    // O. Creo buffer si no vino cargado => si vino cargado, se supone que ya tiene el size que considera los registros que se cargan en esta función. Esto tiene lógica para cuando se necesite pasar otros valores en el buffer además del contexto de ejecución, como por ejemplo en la INST WAIT que se pasa el recurso que se quiere utilizar. 
    if(*buffer_contexto_proceso == NULL){ // por defecto, si no vino nada cargado, siempre desalojo con contexto de ejecucion = registros 
        *buffer_contexto_proceso = buffer_create(
            sizeof(uint32_t) * 8 // REGISTROS: PC, EAX, EBX, ECX, EDX, SI, DI
            + sizeof(uint8_t) * 4 // REGISTROS: AX, BX, CX, DX
        );
    }
    // 1. Cargo buffer con contexto de ejecución del proceso en el momento del desalojo
    buffer_add_registros(*buffer_contexto_proceso, &(registros));
    // 2. Envió el contexto de ejecución del proceso y el motivo de desalojo (código de operación del paquete) a KERNEL
    cargar_paquete(conexion_kernel, mensaje_desalojo, *buffer_contexto_proceso); // esto ya desaloja el buffer!
}

void* recibir_interrupcion(void* conexion){
    int interrupcion_kernel, servidor_interrupt = *(int*) conexion;
    interrupcion_kernel = esperar_cliente(servidor_interrupt);
    int cod_op;
    while((cod_op = recibir_operacion(interrupcion_kernel)) != -1){
        switch (cod_op){
        case CONEXION:
            recibir_conexion(interrupcion_kernel);
            break;
        case INTERRUPCION:
            
            t_sbuffer *buffer_interrupt = cargar_buffer(interrupcion_kernel);
            
            // guarda datos del buffer (contexto de proceso)
            t_pic *interrupcion_recibida = malloc(sizeof(t_pic));
            if(interrupcion_recibida != NULL){
                // guardo PID del proceso que se va a ejecutar
                interrupcion_recibida->pid = buffer_read_uint32(buffer_interrupt);
                interrupcion_recibida->motivo_interrupcion = buffer_read_int(buffer_interrupt);
                interrupcion_recibida->bit_prioridad = buffer_read_uint8(buffer_interrupt);
                
                // ANTES DE AGREGAR LA INTERRUPCION A LA LISTA DE INTERRUPCIONES, DEBE VERIFICAR QUE EL PROCESO NO SE HAYA DESALOJADO PREVIAMENTE
                sem_wait(&mutex_lista_interrupciones);
                list_add(list_interrupciones, interrupcion_recibida);
                sem_post(&mutex_lista_interrupciones);
                log_debug(logger, "Recibi una interrupcion para el proceso %u, por %d", interrupcion_recibida->pid, interrupcion_recibida->motivo_interrupcion);
            }
            buffer_destroy(buffer_interrupt);
            break;
        case -1:
            log_error(logger, "cliente desconectado");
            break;
        default:
            log_warning(logger, "Operacion desconocida.");
            break;
        }
    }
    return NULL;
}

t_sbuffer* mmu(const char* operacion, uint32_t direccion_logica, uint32_t bytes_peticion, void* dato_escribir) {
    void* dato_a_escribir = dato_escribir;
    int nro_pagina = (int)floor(direccion_logica/TAM_PAGINA);
    int desplazamiento = direccion_logica - (nro_pagina * TAM_PAGINA);
    
    int cantidad_peticiones_memoria = (int)ceil((double)(desplazamiento + bytes_peticion)/TAM_PAGINA);  // necesitas leer/escribir X paginas

    uint32_t tamanio_buffer = sizeof(uint32_t) + // pid proceso!
        sizeof(int) + // cantidad de peticiones
        sizeof(uint32_t) * cantidad_peticiones_memoria + // direccion/es fisica/s = nro_marco * tam_pagina + desplazamiento 
        sizeof(uint32_t) * cantidad_peticiones_memoria; // bytes de lectura/escritura por petición (para el void* su tamanio o para lectura sólo bytes)
    if(strcmp(operacion, "ESCRIBIR") == 0)
        tamanio_buffer += bytes_peticion; // le agrego el tamanio total de todos los void* que voy a pasar

    t_sbuffer* buffer_direcciones_fisicas = buffer_create(tamanio_buffer);

    buffer_add_uint32(buffer_direcciones_fisicas, pid_proceso);
    buffer_add_int(buffer_direcciones_fisicas, cantidad_peticiones_memoria);
    t_tlb* entrada_tlb;
    uint32_t marco; 

    log_debug(logger, "cantidad peticiones %d para dir.logica %u y bytes %u", cantidad_peticiones_memoria, direccion_logica, bytes_peticion);
    if(cantidad_peticiones_memoria == 1){
        // 1. Verifica si la página está en la TLB
        entrada_tlb = buscar_marco_tlb(pid_proceso, nro_pagina);
        if(!entrada_tlb){
            if(TLB_HABILITADA) log_info(logger, "PID: %u - TLB MISS - Pagina: %d", pid_proceso, nro_pagina);
            // A. Enviar petición a memoria para acceder a tabla de páginas pasando número de página y pid (espera respuesta informando marco)
            marco = solicitar_marco_a_memoria(pid_proceso, nro_pagina);
            // B. Agregar entrada a TLB según algoritmo de TLB agregar_marco_tlb
            if(TLB_HABILITADA) entrada_tlb = agregar_marco_tlb(pid_proceso, nro_pagina, marco);
        } else{
            log_info(logger, "PID: %u - TLB HIT - Pagina: %d", pid_proceso, nro_pagina);
            marco = entrada_tlb->marco;
            if(strcmp(config.algoritmo_tlb, "LRU") == 0) {
                entrada_tlb->timestamp = obtener_timestamp(); // si es LRU actualiza el timestamp con la última consulta
                //log_debug(logger, "se actualizo el timestamp de la entrada a la tlb");
            }
        }

        uint32_t direccion_fisica = marco * TAM_PAGINA + desplazamiento;
        buffer_add_uint32(buffer_direcciones_fisicas, direccion_fisica);
        if(strcmp(operacion, "ESCRIBIR") == 0) 
            buffer_add_void(buffer_direcciones_fisicas, dato_a_escribir, bytes_peticion); // el void* + su tamanio (bytes_peticion)
        else 
            buffer_add_uint32(buffer_direcciones_fisicas, bytes_peticion); // sólo su tamanio si es lectura
        
    } else {
        void* datos_a_enviar = NULL; // acá se va a reservar el "cacho" del dato que se va a pasar por petición (si es que la peticion es de tipo ESCRIBIR)
        void* dato_enviar = dato_a_escribir; // guardo una referencia del dato completo que se va a enviar para pasarlo de a partes
        uint32_t bytes_pendientes = bytes_peticion;
        uint32_t bytes_a_enviar_por_peticion = 0;
        uint32_t bytes_enviados = 0;
        int peticiones_memoria = cantidad_peticiones_memoria; 
        for (int i = 0; i < cantidad_peticiones_memoria; i++){
           if(i == 0) // en esta primera vuelta consumo el espacio restante a partir del offset de la primera página
                bytes_a_enviar_por_peticion = TAM_PAGINA - desplazamiento; // los bytes que entran en la primera página a partir del offset dado
           else { // en las demás vueltas, consumo toda la página o lo que quede de los bytes pendientes ya que el offset es 0
                if(i < (peticiones_memoria - 1))
                    bytes_a_enviar_por_peticion = TAM_PAGINA;
                else 
                    bytes_a_enviar_por_peticion = bytes_pendientes;
           }

            // 1. Verifica existencia en TLB
            entrada_tlb = buscar_marco_tlb(pid_proceso, nro_pagina);
            if(!entrada_tlb){
                if(TLB_HABILITADA) log_info(logger, "PID: %u - TLB MISS - Pagina: %d", pid_proceso, nro_pagina);
                // 1. Enviar petición a memoria para acceder a tabla de páginas pasando número de página y pid (espera respuesta informando marco)
                marco = solicitar_marco_a_memoria(pid_proceso, nro_pagina);
                // B. Agregar entrada a TLB según algoritmo de TLB agregar_marco_tlb
                if(TLB_HABILITADA) entrada_tlb = agregar_marco_tlb(pid_proceso, nro_pagina, marco);
            } else{
                log_info(logger, "PID: %u - TLB HIT - Pagina: %d", pid_proceso, nro_pagina);
                marco = entrada_tlb->marco;
                if(strcmp(config.algoritmo_tlb, "LRU") == 0) {
                    entrada_tlb->timestamp = obtener_timestamp(); // si es LRU actualiza el timestamp con la última consulta
                    //log_debug(logger, "se actualizo el timestamp de la entrada a la tlb");
                }
            }

            uint32_t direccion_fisica = marco * TAM_PAGINA + desplazamiento;
            buffer_add_uint32(buffer_direcciones_fisicas, direccion_fisica);
            if(strcmp(operacion, "ESCRIBIR") == 0) {
                // Toma del contenido de dato_enviar los primeros N (=bytes_a_enviar_por_peticion) bytes y deja el resto almacenado en dato_enviar para la próxima peticion de escritura
                datos_a_enviar = malloc(bytes_a_enviar_por_peticion);
                memcpy(datos_a_enviar, dato_enviar + bytes_enviados, bytes_a_enviar_por_peticion);
                buffer_add_void(buffer_direcciones_fisicas, datos_a_enviar, bytes_a_enviar_por_peticion);
                free(datos_a_enviar); // para la próxima petición
            } else
                buffer_add_uint32(buffer_direcciones_fisicas, bytes_a_enviar_por_peticion); // sólo su tamanio si es lectura


            bytes_pendientes-= bytes_a_enviar_por_peticion;
            bytes_enviados+=bytes_a_enviar_por_peticion;
            nro_pagina++; // próxima petición a la página siguiente
            desplazamiento = 0; // las páginas nuevas siempre parten de su offset 0
        }
    }
    return buffer_direcciones_fisicas;
}

uint64_t obtener_timestamp() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000000ULL + tv.tv_usec;
}

uint32_t solicitar_marco_a_memoria(uint32_t proceso, int pagina){
    t_sbuffer* buffer_marco = buffer_create(
        sizeof(uint32_t) + // pid proceso
        sizeof(int) // pagina
    );

    buffer_add_uint32(buffer_marco, proceso);
    buffer_add_int(buffer_marco, pagina);
    cargar_paquete(conexion_memoria, TLB_MISS, buffer_marco);

    int respuesta_memoria_marco = recibir_operacion(conexion_memoria);
    if(respuesta_memoria_marco == MARCO_SOLICITADO){
        t_sbuffer* buffer_marco_solicitado = cargar_buffer(conexion_memoria);
        uint32_t marco = buffer_read_uint32(buffer_marco_solicitado);
        log_info(logger, "PID: %u - OBTENER MARCO - Página: %d - Marco: %u", proceso, pagina, marco);
        buffer_destroy(buffer_marco_solicitado);
        return marco;
    } // TODO: considerar errores!!!

    return 0;
}

void remover_entrada_segun_algoritmo(){
    int index_mas_viejo = -1;
    uint64_t min_timestamp = UINT64_MAX;
    for (int i = 0; i < list_size(tlb_list); i++) {
        t_tlb* entrada = (t_tlb*)list_get(tlb_list, i);
        if (entrada->timestamp < min_timestamp) {
            min_timestamp = entrada->timestamp;
            index_mas_viejo = i;
        }
    }

    t_tlb* entrada_eliminada = (t_tlb*)list_remove(tlb_list, index_mas_viejo);
    if (entrada_eliminada != NULL) {
        //log_debug(logger, "se elimina la entrada del proceso %u pagina %d", entrada_eliminada->pid, entrada_eliminada->pagina);
        free(entrada_eliminada);
    }
}

t_tlb* agregar_marco_tlb(uint32_t proceso, int pagina, uint32_t marco){
    // si la TLB ya está llena => sustitución por algoritmo (se saca la entrada con timestamp más viejo)
    if(list_size(tlb_list) == config.cantidad_entradas_tlb)
        remover_entrada_segun_algoritmo();

    t_tlb* nueva_entrada_tlb = malloc(sizeof(t_tlb));
    nueva_entrada_tlb->pid = proceso;
    nueva_entrada_tlb->pagina = pagina;
    nueva_entrada_tlb->marco = marco;
    nueva_entrada_tlb->timestamp = obtener_timestamp();

    list_add(tlb_list, nueva_entrada_tlb);
    return nueva_entrada_tlb;
}

t_tlb* buscar_marco_tlb(uint32_t proceso, uint32_t nro_pagina){
    bool comparar_pid_pagina(void *elemento){
        t_tlb *entrada_tlb = (t_tlb *)elemento;
        return (entrada_tlb->pid == proceso && entrada_tlb->pagina == nro_pagina);
    }
    t_tlb* encontrado = (t_tlb*)list_find(tlb_list, comparar_pid_pagina);
    return encontrado; 
}

// la dejo vacía y declarada porque, a pesar de que no la requeramos acá, como importamos el utilsServer.c necesita de una defición de esta función, hay que ver qué conviene más adelante
void* atender_cliente(void* cliente){
    return NULL;
}
