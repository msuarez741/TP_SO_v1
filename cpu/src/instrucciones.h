#include <stdlib.h>
#include <stdio.h>
#include <utils/hello.h>
#include <utils/buffer.h>
#include <commons/string.h>
#include <commons/log.h>

typedef enum {
    _UINT8,
    _UINT32
} tiposDeDato;

extern t_registros_cpu registros; 



int contar_digitos(int num);
char* intToCadena(int num, char* array, int longitud);
tiposDeDato obtenerTipo(char* registro);
void obtenerDireccionMemoria(char* registro, uint8_t** dato_8bits, uint32_t** dato_32bits);
void set(char* registro, char* valor);
void MOV_IN (char* registroDato, char* registroDireccion);
void MOV_OUT (char* registroDireccion, char* registroDato);
void SUM (char* registroDireccion, char* registroOrigen);
void SUB (char* registroDireccion, char* registroOrigen);
void decode_instruccion(t_sbuffer* buffer_instruccion, t_log* logger);

