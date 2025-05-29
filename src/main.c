#include "args.h"
#include "encoder.h"
#include "decoder.h"
#include "fileutils.h"
#include <color.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static void PrintCompressionStats(FileList fileList, const char outputPath[])
{
    long inSize = 0;
    for (int i = 0; i < fileList.count; i++)
        inSize += GetFileSize(fileList.paths[i]);

    long outSize = GetFileSize(outputPath);

    if (inSize < 0 || outSize < 0)
    {
        fprintf(stderr, COLOR_STR("Cannot compute compression stats.\n", RED));
        return;
    }

    printf("\n--- Compression stats ---\n");
    printf("Input file(s) size:   %ld bytes\n", inSize);
    printf("Output archive size:  %ld bytes\n", outSize);
    if (inSize > 0)
        printf("Compression ratio:    %.2f%%\n", 100.0 * inSize / outSize);
    printf("-------------------------\n");
}

int main(int argc, char *argv[])
{
    ParsedArgs *args = parse_args(argc, argv);

    switch (args->mode)
    {
        case MODE_HELP:
            print_usage(argv[0]);
            break;

        case MODE_COMPRESS:
        {
            if (args->num_input_paths == 0 || !args->output_path)
                print_error_and_exit("Missing input or output path", argv[0]);

            // Собрать список всех файлов из директорий
            FileList inputFiles;
            inputFiles.paths = NULL;
            inputFiles.count = 0;

            for (size_t i = 0; i < args->num_input_paths; ++i)
            {
                if (IsDirectory(args->input_paths[i]))
                {
                    FileList list = GetFilesInDirectory(args->input_paths[i]);
                    size_t oldCount = inputFiles.count;
                    inputFiles.count += list.count;
                    inputFiles.paths = realloc(inputFiles.paths, inputFiles.count * sizeof(char *));
                    for (size_t j = 0; j < list.count; ++j)
                    {
                        inputFiles.paths[oldCount + j] = malloc(sizeof(char) * (strlen(list.paths[j]) + 1));
                        strcpy(inputFiles.paths[oldCount + j], list.paths[j]);
                    }
                    FreeFileList(list);
                }
                else
                {
                    inputFiles.count++;
                    inputFiles.paths = realloc(inputFiles.paths, inputFiles.count * sizeof(char*));
                    inputFiles.paths[inputFiles.count - 1] = malloc(sizeof(char) * (strlen(args->input_paths[i]) + 1));
                    strcpy(inputFiles.paths[inputFiles.count - 1], args->input_paths[i]);
                }
            }

            int result = EncodeFiles(args, (const char**)inputFiles.paths, inputFiles.count, args->output_path, args->symbol_size);

            if (result == 0)
                PrintCompressionStats(inputFiles, args->output_path);
            else
                fprintf(stderr, "Compression failed.\n");

            FreeFileList(inputFiles);
            break;
        }

        case MODE_DECOMPRESS:
        {
            if (args->num_input_paths == 0 || !args->output_path)
                print_error_and_exit("Missing input or output path", argv[0]);

            const char *archive = args->input_paths[0];
            const char **wanted = NULL;
            size_t wantedCount = 0;

            if (args->num_input_paths > 1)
            {
                wanted = (const char **)(args->input_paths + 1);
                wantedCount = args->num_input_paths - 1;
            }

            int res = DecodeArchive(archive, args->output_path, wanted, wantedCount, wantedCount == 0);
            if (res != 0)
                fprintf(stderr, "Decompression failed.\n");
            break;
        }

        default:
            print_error_and_exit("Invalid or missing mode", argv[0]);
    }

    free_parsed_args(args);
    return 0;
}
