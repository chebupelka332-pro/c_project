#include "encoder.h"
#include "bitstream.h"
#include "huffman.h"
#include "fileutils.h"
#include "args.h"
#include <color.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <linux/limits.h>

#define MAGIC_BYTES "HUFF"
#define ARCHIVE_VERSION 1
#define PADDING_BYTE 0x00 // Байт для дополнения последнего символа при symbol_size=2 и нечетном размере файла

static void BitWriterWriteUint64(BitWriter *writer, uint64_t value)
{
    BitWriterWriteBits(writer, (unsigned int)(value >> 32), 32);
    BitWriterWriteBits(writer, (unsigned int)(value & 0xFFFFFFFFU), 32);
}

static printProgress(uint64_t bytesProcessed, uint64_t fileSize, const char fileName[])
{
    // Индикатор прогресса
    if (bytesProcessed == fileSize)
    {
        printf("\r  Encoding %s: %llu / %llu bytes (%.2f%%)", fileName, (unsigned long long)bytesProcessed, (unsigned long long)fileSize,
               fileSize > 0 ? (double)bytesProcessed * 100.0 / fileSize : 100.0);
        fflush(stdout);
    }
}

int EncodeFiles(ParsedArgs *cmd_args, const char **inputPaths, size_t numInputPaths, const char *outputPath, uint32_t symbol_size)
{
    if (!cmd_args || !inputPaths || numInputPaths == 0 || !outputPath)
    {
        fprintf(stderr, COLOR_STR("Error: Invalid arguments to EncodeFiles.\n", RED));
        return 1;
    }
    if (symbol_size != 1 && symbol_size != 2)
    {
        fprintf(stderr, COLOR_STR("Error: Invalid symbol_size (%u). Must be 1 or 2.\n", RED), symbol_size);
        return 1;
    }

    BitWriter *writer = BitWriterOpen(outputPath);
    if (!writer)
    {
        perror(COLOR_STR("Error opening output archive for writing", RED));
        return 1;
    }

    // Запись заголовка архива
    for (size_t i = 0; i < strlen(MAGIC_BYTES); ++i)
        BitWriterWriteBits(writer, MAGIC_BYTES[i], 8);

    BitWriterWriteBits(writer, ARCHIVE_VERSION, 8);
    BitWriterWriteBits(writer, (uint8_t)symbol_size, 8);
    BitWriterWriteBits(writer, (uint32_t)numInputPaths, 32);

    for (size_t i = 0; i < numInputPaths; ++i)
    {
        const char *currentFilePath = inputPaths[i];
        const char *fileNameInArchive = NULL;

        // Определение имени файла для сохранения в архиве (и обработка относительных путей)
        char bestBasePath[PATH_MAX] = {0};
        int bestBasePathLen = -1;

        for (size_t j = 0; j < cmd_args->num_input_paths; ++j)
        {
            const char *original_arg_path = cmd_args->input_paths[j];
            size_t original_arg_len = strlen(original_arg_path);

            if (IsDirectory(original_arg_path))
            {
                if (strncmp(currentFilePath, original_arg_path, original_arg_len) == 0)
                {
                    if (currentFilePath[original_arg_len] == '\0' || currentFilePath[original_arg_len] == '/')
                    {
                        if ((int)original_arg_len > bestBasePathLen)
                        {
                            strncpy(bestBasePath, original_arg_path, sizeof(bestBasePath) - 1);
                            bestBasePath[sizeof(bestBasePath) - 1] = '\0';
                            bestBasePathLen = original_arg_len;
                        }
                    }
                }
            }
        }

        if (bestBasePathLen != -1)
        {
            fileNameInArchive = currentFilePath + bestBasePathLen;
            if (*fileNameInArchive == '/')
                fileNameInArchive++;
            if (*fileNameInArchive == '\0')
                fileNameInArchive = GetFileName(currentFilePath);
        }
        else
            fileNameInArchive = GetFileName(currentFilePath);

        printf("Processing file %zu/%zu: %s (archiving as: %s)\n", i + 1, numInputPaths, GetFileName(currentFilePath), fileNameInArchive);

        FILE *inFile = fopen(currentFilePath, "rb");
        if (!inFile)
        {
            perror(COLOR_STR("Error opening input file", RED));
            fprintf(stderr, COLOR_STR("Failed file: %s\n", RED), currentFilePath);
            BitWriterClose(writer);
            remove(outputPath);
            return 1;
        }

        uint64_t fileSize = GetFileSize(currentFilePath);
        if (fileSize == (uint64_t)-1)
        {
            fprintf(stderr, COLOR_STR("Error getting size of file: %s\n", RED), currentFilePath);
            fclose(inFile);
            BitWriterClose(writer);
            remove(outputPath);
            return 1;
        }

        // Запись метаданных файла в архив
        size_t fileNameLen = strlen(fileNameInArchive);
        BitWriterWriteBits(writer, (uint16_t)fileNameLen, 16);
        for (size_t k = 0; k < fileNameLen; ++k)
            BitWriterWriteBits(writer, fileNameInArchive[k], 8);

        BitWriterWriteUint64(writer, fileSize);

        HuffCode *huff_codes = NULL;
        if (fileSize > 0)
        {
            huff_codes = GenerateCodes(inFile, fileSize, symbol_size);
            if (!huff_codes)
            {
                fprintf(stderr, COLOR_STR("Error generating Huffman codes for %s.\n", RED), fileNameInArchive);
                fclose(inFile);
                BitWriterClose(writer);
                remove(outputPath);
                return 1;
            }
            if (fseek(inFile, 0, SEEK_SET) != 0)
            {
                perror(COLOR_STR("Error rewinding input file (GenerateCodes is expected to read to EOF, encoder must rewind)", RED));
                free(huff_codes);
                fclose(inFile);
                BitWriterClose(writer);
                remove(outputPath);
                return 1;
            }
        }
        else
            printf(COLOR_STR("  File %s is empty. Storing as empty.\n", YELLOW), fileNameInArchive);

        // Запись таблицы Хаффмана
        uint32_t alphabet_cardinality = (1 << (symbol_size * 8));
        uint16_t active_codes_count = 0;

        if (huff_codes)
        {
            for (uint32_t sym_val_idx = 0; sym_val_idx < alphabet_cardinality; ++sym_val_idx)
            {
                if (huff_codes[sym_val_idx].code_len > 0)
                {
                    active_codes_count++;
                }
            }
        }
        BitWriterWriteBits(writer, active_codes_count, 16);

        if (huff_codes)
        {
            for (uint32_t sym_val_idx = 0; sym_val_idx < alphabet_cardinality; ++sym_val_idx)
            {
                if (huff_codes[sym_val_idx].code_len > 0)
                {
                    if (symbol_size == 1)
                        BitWriterWriteBits(writer, (uint8_t)sym_val_idx, 8);
                    else if (symbol_size == 2)
                        BitWriterWriteBits(writer, (uint16_t)sym_val_idx, 16);

                    BitWriterWriteBits(writer, huff_codes[sym_val_idx].code_len, 8);
                    BitWriterWriteBits(writer, huff_codes[sym_val_idx].code, huff_codes[sym_val_idx].code_len);
                }
            }
        }

        if (fileSize > 0 && huff_codes)
        {
            uint64_t bytes_processed = 0;
            unsigned char symbol_read_buffer[2];

            while (bytes_processed < fileSize)
            {
                uint16_t current_symbol_val;

                size_t bytes_to_read_this_iteration = symbol_size;
                // Обработка последнего неполного символа (если symbol_size=2 и размер файла нечетный)
                if (fileSize - bytes_processed < symbol_size)
                    bytes_to_read_this_iteration = fileSize - bytes_processed;

                size_t bytes_actually_read_from_file = fread(symbol_read_buffer, 1, bytes_to_read_this_iteration, inFile);

                if (bytes_actually_read_from_file == 0)
                {
                    if (feof(inFile) && bytes_processed == fileSize)
                        break;
                    perror(COLOR_STR("Error reading input file during encoding content", RED));
                    free(huff_codes);
                    fclose(inFile);
                    BitWriterClose(writer);
                    remove(outputPath);
                    return 1;
                }

                if (symbol_size == 1)
                    current_symbol_val = symbol_read_buffer[0];
                else if (symbol_size == 2)
                {
                    if (bytes_actually_read_from_file == 1) // Последний байт файла с 2-байтными символами и требуется дополнение
                        current_symbol_val = ((uint16_t)symbol_read_buffer[0] << 8) | PADDING_BYTE;
                    else // Прочитано 2 байта
                        current_symbol_val = ((uint16_t)symbol_read_buffer[0] << 8) | symbol_read_buffer[1];
                }

                HuffCode hc = huff_codes[current_symbol_val];
                if (hc.code_len > 0)
                    BitWriterWriteBits(writer, hc.code, hc.code_len);

                bytes_processed += bytes_actually_read_from_file;

                /* Индикатор прогресса
                if (bytes_processed % 102400 == 0 || bytes_processed == fileSize)
                {
                    printf("\r   Encoding %s: %llu / %llu bytes (%.2f%%)",
                           fileNameInArchive, (unsigned long long)bytes_processed,
                           (unsigned long long)fileSize,
                           fileSize > 0 ? (double)bytes_processed * 100.0 / fileSize : 100.0);
                    fflush(stdout);
                }*/
               printProgress(bytes_processed, fileSize, fileNameInArchive);
            }
            printf("\n");
        }
        printf("\n");
        fclose(inFile);
        if (huff_codes)
            free(huff_codes);
    }

    BitWriterClose(writer);
    printf(COLOR_STR("All files processed. Archive created: %s\n", GREEN), outputPath);
    return 0;
}