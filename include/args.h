#ifndef ARGS_H
#define ARGS_H

#include <stdint.h>
#include <stdlib.h>

typedef enum 
{
    MODE_NONE,       // Режим не задан (ошибка или ожидание ввода)
    MODE_COMPRESS,   // Режим сжатия
    MODE_DECOMPRESS, // Режим распаковки
    MODE_HELP        // Режим вывода справки
} OperationMode;

typedef struct 
{
    OperationMode mode;         // Режим работы (compress, decompress, help)
    char *output_path;          // Путь к выходному файлу/директории (дублируется)
    size_t num_input_paths;     // Количество входных путей
    char **input_paths;         // Массив путей к входным файлам/директориям (дублируются) 
    uint32_t symbol_size;      // Размер символа в байтах (1 или 2). Актуален только для сжатия.
} ParsedArgs;


void print_error_and_exit(const char *message, const char *program_name);
void print_usage(const char *program_name);
void free_parsed_args(ParsedArgs *args);
ParsedArgs* parse_args(int argc, char *argv[]);

#endif