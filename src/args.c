#include "color.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>

#define OUTPUT_ARG "-o"
#define SYMBOL_SIZE_ARG "-s"
#define COMPRESS_ARG "-c"
#define DECOMPRESS_ARG "-d"
#define HELP_ARG "--help"


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

void print_usage(const char *program_name) 
{
    printf("Usage: %s [OPTIONS] <INPUT_PATHS...>\n", program_name);
    printf("Huffman archiver/unarchiver.\n\n");
    printf("Options:\n");
    printf("  %s\tCompress mode.\n", COMPRESS_ARG);
    printf("  %s\tDecompress mode.\n", DECOMPRESS_ARG);
    printf("  %s <output_path>\tOutput file (compress) or directory (decompress).\n", OUTPUT_ARG);
    printf("\tMandatory for compression. Optional for decompression (defaults to current dir).\n");
    printf("  %s <1|2>\tSymbol size in bytes (1 or 2). Default is 1. Only for compression.\n", SYMBOL_SIZE_ARG);
    printf("  %s\tShow this help message.\n", HELP_ARG);
    printf("\nInput Paths:\n");
    printf("  For compress (%s): One or more files OR exactly one directory.\n", COMPRESS_ARG);
    printf("  For decompress (%s): Exactly one archive file.\n", DECOMPRESS_ARG);
    printf("\nExamples:\n");
    printf("  %s -c -o archive.huff file1.txt image.jpg\n", program_name);
    printf("  %s -c -s 2 -o archive.huff large_binary_data\n", program_name);
    printf("  %s -c -o archive.huff my_folder/\n", program_name);
    printf("  %s -d -o unpacked_files/ archive.huff\n", program_name);
    printf("  %s -d archive.huff\n", program_name);
    printf("  %s --help\n", program_name);
}

void print_error_and_exit(const char *message, const char *program_name) 
{
    fprintf(stderr, COLOR_STR("Error: %s\n", RED), message);
    exit(EXIT_FAILURE);
}

void free_parsed_args(ParsedArgs *args) 
{
    if (args == NULL)
        return;

    if (args->output_path != NULL)
    {
        free(args->output_path);
        args->output_path = NULL;
    }
    
    if (args->input_paths != NULL) 
    {
        for (size_t i = 0; i < args->num_input_paths; i++) 
        {
            if (args->input_paths[i] != NULL) 
            {
                free(args->input_paths[i]);
                args->input_paths[i] = NULL;
            }
        }
        free(args->input_paths);
        args->input_paths = NULL;
    }
    args->num_input_paths = 0;
    free(args);
}

static void validate_args(ParsedArgs args, const char* program_name)
{
    if (args.mode == MODE_NONE)
        print_error_and_exit("No operation mode specified (-c or -d).", program_name);

    if (args.mode == MODE_COMPRESS)
    {
        // Для сжатия обязателен -o
        if (args.output_path == NULL)
            print_error_and_exit("Output path (-o) is mandatory for compression.", program_name);

        // Для сжатия должны быть входные пути
        if (args.num_input_paths == 0)
            print_error_and_exit("No input files or directory specified for compression.", program_name);
    } 
    else if (args.mode == MODE_DECOMPRESS)
    {
            // Для распаковки должен быть ровно один входной путь (архивный файл)
            if (args.num_input_paths != 1)
                print_error_and_exit("Decompression requires exactly one input archive file.", program_name);

            if (args.symbol_size != 0U)
                print_error_and_exit("-s option is only valid for compression mode (-c).", program_name);
    }
}

ParsedArgs* parse_args(int argc, char *argv[])
{
    ParsedArgs *args = malloc(sizeof(ParsedArgs));

    args->mode = MODE_NONE;
    args->output_path = NULL;
    args->input_paths = NULL;
    args->num_input_paths = 0;
    args->symbol_size = 0;

    const char *program_name = argv[0];

    if (argc == 1) // Если аргументов нет (только имя программы), показать справку
    {
        printf(COLOR_STR(LOGO, LIGHT_BLUE));
        exit(EXIT_SUCCESS);
    }

    // Предварительный проход для поиска --help
    for (int i = 1; i < argc; ++i) 
    {
        if (strcmp(argv[i], HELP_ARG) == 0) 
        {
            print_usage(program_name);
            exit(EXIT_SUCCESS);
        }
    }

    // Парсинг аргументов
    for (int i = 1; i < argc; ++i) 
    {
        if (strcmp(argv[i], COMPRESS_ARG) == 0) 
        {
            if (args->mode != MODE_NONE)
                print_error_and_exit("Cannot specify both -c and -d.", program_name);
            args->mode = MODE_COMPRESS;
        } 
        else if (strcmp(argv[i], DECOMPRESS_ARG) == 0) 
        {
            if (args->mode != MODE_NONE)
                print_error_and_exit("Cannot specify both -c and -d.", program_name);
            args->mode = MODE_DECOMPRESS;
        } 
        else if (strcmp(argv[i], OUTPUT_ARG) == 0) 
        {
            if (args->output_path != NULL)
                print_error_and_exit("Output path specified multiple times.", program_name);

            if (i + 1 >= argc)
                print_error_and_exit("Missing argument for -o.", program_name);

            args->output_path = malloc(sizeof(char) * (strlen(argv[i+1]) + 1));
            strcpy(args->output_path, argv[i+1]);

            if (args->output_path == NULL)
                print_error_and_exit("Memory allocation failed.", program_name);
            i++;
        }
        else if (strcmp(argv[i], SYMBOL_SIZE_ARG) == 0)
        {
            if (i + 1 >= argc)
                print_error_and_exit("Missing argument for -s.", program_name);

            if (args->symbol_size != 0) // Проверяем, не задан ли уже размер символа
                print_error_and_exit("Symbol size specified multiple times.", program_name);
            
            uint32_t size = atoi(argv[i+1]);

            if (size != 1 && size != 2)
                print_error_and_exit("Invalid value for -s. Must be 1 or 2.", program_name);

            args->symbol_size = size;
            i++;
        }
        else 
        {
            // Если это не известный флаг, считаем это входным путем
            char **temp = realloc(args->input_paths, (args->num_input_paths + 1) * sizeof(char*));
            if (temp == NULL) 
            {
                free_parsed_args(args);
                print_error_and_exit("Memory allocation failed for input paths.", program_name);
            }
            args->input_paths = temp;

            // Дублируем строку пути
            args->input_paths[args->num_input_paths] = malloc(sizeof(char) * (strlen(argv[i]) + 1));
            strcpy(args->input_paths[args->num_input_paths], argv[i]);

            if (args->input_paths[args->num_input_paths] == NULL) 
            {
                free_parsed_args(args);
                print_error_and_exit("Memory allocation failed for input path string.", program_name);
            }
            args->num_input_paths++;
        }
    }

    if (args->mode == MODE_COMPRESS && args->symbol_size == 0)
        args->symbol_size = 1;

    validate_args(*args, program_name);

    return args;
}