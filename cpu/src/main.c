#include <stdlib.h>
#include <stdio.h>
#include <utils/hello.h>
#include "main.h"

config_struct config;

int main(int argc, char* argv[]) {
    
    int conexion_memoria;
    t_config* archivo_config = iniciar_config("cpu.config");    
    t_log* logger = log_create("cpu.log", "CPU", 1, LOG_LEVEL_INFO);

    cargar_config_struct_CPU(archivo_config);

    decir_hola("CPU");

    //Servidor CPU
    //dispatch
    int socket_servidor_dispatch = iniciar_servidor(config.puerto_escucha_dispatch);
    log_info(logger, config.puerto_escucha_dispatch);
    log_info(logger, "Server CPU DISPATCH");

    int cliente_dispatch = esperar_cliente(socket_servidor_dispatch);
    bool salir = false;
    while(!salir){
        int cod_op = recibir_operacion(cliente_dispatch);
        switch (cod_op)
        {
        case CONEXION:
            recibir_conexion(cliente_dispatch);
            break;
        case -1:
            log_error(logger, "cliente desconectado de DISPATCH");
            salir = true;
            break;
        default:
            log_warning(logger, "Operacion desconocida.");
            break;
        }
    }

    //interrupt
    int socket_servidor_interrupt = iniciar_servidor(config.puerto_escucha_interrupt);
    log_info(logger, config.puerto_escucha_interrupt);
    log_info(logger, "Server CPU INTERRUPT"); 

    int cliente_interrupt = esperar_cliente(socket_servidor_interrupt);
    salir = false;
    while(!salir){
        int cod_op = recibir_operacion(cliente_interrupt);
        switch (cod_op)
        {
        case CONEXION:
            recibir_conexion(cliente_interrupt);
            break;
        case -1:
            log_error(logger, "cliente desconectado de INT");
            salir = true;
            break;
        default:
            log_warning(logger, "Operacion desconocida.");
            break;
        }
    }

    //Conexion con Memoria
    conexion_memoria = crear_conexion(config.ip_memoria, config.puerto_memoria);
    enviar_conexion("CPU", conexion_memoria);

    //Limpieza
    log_destroy(logger);
	config_destroy(archivo_config);
    liberar_conexion(conexion_memoria);

    return 0;
}

void cargar_config_struct_CPU(t_config* archivo_config){
    config.ip_memoria = config_get_string_value(archivo_config, "IP_MEMORIA");
    config.puerto_memoria = config_get_string_value(archivo_config, "PUERTO_MEMORIA");
    config.puerto_escucha_dispatch = config_get_string_value(archivo_config, "PUERTO_ESCUCHA_DISPATCH");
    config.puerto_escucha_interrupt = config_get_string_value(archivo_config, "PUERTO_ESCUCHA_INTERRUPT");
    config.cantidad_entradas_tlb = config_get_string_value(archivo_config, "CANTIDAD_ENTRADAS_TLB");
    config.algoritmo_tlb = config_get_string_value(archivo_config, "ALGORITMO_TLB");
}

