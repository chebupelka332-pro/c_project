#include "decoder.h"
#include "bitstream.h"
#include "fileutils.h"
#include "args.h"
#include <color.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <linux/limits.h>

#define MAGIC_BYTES_EXPECTED "HUFF"
#define ARCHIVE_VERSION_EXPECTED 1

typedef struct DecodingTreeNode
{
    struct DecodingTreeNode *child0;
    struct DecodingTreeNode *child1;
    int is_leaf;
    uint16_t symbol;
} DecodingTreeNode;

static DecodingTreeNode *CreateDecodingNode()
{
    DecodingTreeNode *node = (DecodingTreeNode *)malloc(sizeof(DecodingTreeNode));
    if (node)
    {
        node->child0 = NULL;
        node->child1 = NULL;
        node->is_leaf = 0;
        node->symbol = 0;
    }
    return node;
}

static void FreeDecodingTree(DecodingTreeNode *node)
{
    if (!node)
        return;
    FreeDecodingTree(node->child0);
    FreeDecodingTree(node->child1);
    free(node);
}

static int InsertIntoDecodingTree(DecodingTreeNode *root, uint16_t symbol_val, uint64_t code, uint8_t code_len)
{
    DecodingTreeNode *current = root;
    if (code_len == 0)
    {
        if (root->is_leaf && root->symbol != symbol_val)
        {
            fprintf(stderr, COLOR_STR("Error: Root is already a leaf with a different symbol, cannot insert zero-length code for symbol %u\n", RED), symbol_val);
            return 0;
        }
        if (!root->is_leaf && (root->child0 || root->child1))
        {
            fprintf(stderr, COLOR_STR("Error: Root has children, cannot insert zero-length code for symbol %u\n", RED), symbol_val);
            return 0;
        }
        current->is_leaf = 1;
        current->symbol = symbol_val;
        return 1;
    }

    for (int i = code_len - 1; i >= 0; --i)
    {
        int bit = (code >> i) & 1;
        if (bit == 0)
        {
            if (!current->child0)
            {
                current->child0 = CreateDecodingNode();
                if (!current->child0)
                    return 0; // Ошибка выделения памяти
            }
            current = current->child0;
        }
        else
        {
            if (!current->child1)
            {
                current->child1 = CreateDecodingNode();
                if (!current->child1)
                    return 0; // Ошибка выделения памяти
            }
            current = current->child1;
        }
        if (current->is_leaf && i > 0)
        {
            fprintf(stderr, COLOR_STR("Error: Huffman tree structure conflict: non-leaf node is marked as leaf during path traversal for symbol %u.\n", RED), symbol_val);
            return 0;
        }
    }
    if (current->is_leaf)
    {
        fprintf(stderr, COLOR_STR("Error: Huffman code collision or non-prefix code detected for symbol %u.\n", RED), symbol_val);
        return 0;
    }
    current->is_leaf = 1;
    current->symbol = symbol_val;
    return 1;
}

static uint64_t BitReaderReadUint64(BitReader *reader)
{
    uint32_t high = BitReaderReadBits(reader, 32);
    uint32_t low = BitReaderReadBits(reader, 32);
    return ((uint64_t)high << 32) | low;
}

int DecodeArchive(const char *archivePath, const char *outputDir, const char **wantedFiles, size_t wantedCount, int extractAll)
{
    if (!archivePath || !outputDir)
    {
        fprintf(stderr, COLOR_STR("Error: Invalid arguments to DecodeArchive.\n", RED));
        return 1;
    }

    BitReader *reader = BitReaderOpen(archivePath);
    if (!reader)
    {
        perror(COLOR_STR("Error opening input archive for reading", RED));
        return 1;
    }

    char magic_read[5] = {0};
    for (int i = 0; i < strlen(MAGIC_BYTES_EXPECTED); ++i)
    {
        magic_read[i] = (char)BitReaderReadBits(reader, 8);
    }
    if (strncmp(magic_read, MAGIC_BYTES_EXPECTED, strlen(MAGIC_BYTES_EXPECTED)) != 0)
    {
        fprintf(stderr, COLOR_STR("Error: Not a valid Huffman archive (magic bytes mismatch).\n", RED));
        BitReaderClose(reader);
        return 1;
    }

    uint8_t version = BitReaderReadBits(reader, 8);
    if (version != ARCHIVE_VERSION_EXPECTED)
    {
        fprintf(stderr, COLOR_STR("Error: Unsupported archive version (%u). Expected %u.\n", RED), version, ARCHIVE_VERSION_EXPECTED);
        BitReaderClose(reader);
        return 1;
    }

    uint8_t symbol_size_val = BitReaderReadBits(reader, 8);
    if (symbol_size_val != 1 && symbol_size_val != 2)
    {
        fprintf(stderr, COLOR_STR("Error: Archive contains invalid symbol_size (%u).\n", RED), symbol_size_val);
        BitReaderClose(reader);
        return 1;
    }

    uint32_t num_total_files = BitReaderReadBits(reader, 32);
    printf("Archive contains %u file(s). Symbol size: %u byte(s).\n", num_total_files, symbol_size_val);

    if (CreateDirectoryRecursive(outputDir) != 0)
    {
        fprintf(stderr, COLOR_STR("Error: Could not create output directory: %s (errno: %d, message: %s)\n", RED),
                outputDir, errno, strerror(errno));
        BitReaderClose(reader);
        return 1;
    }

    for (uint32_t file_idx = 0; file_idx < num_total_files; ++file_idx)
    {
        uint16_t filename_len = BitReaderReadBits(reader, 16);
        
        if (filename_len == 0 || filename_len >= PATH_MAX)
        {
            fprintf(stderr, COLOR_STR("Error: Invalid filename length (%u) in archive for file index %u.\n", RED), filename_len, file_idx);
            BitReaderClose(reader);
            return 1;
        }
        char *filename_from_archive = (char *)malloc(filename_len + 1);
        if (!filename_from_archive)
        {
            perror(COLOR_STR("Malloc failed for filename", RED));
            BitReaderClose(reader);
            return 1;
        }
        for (uint16_t k = 0; k < filename_len; ++k)
        {
            filename_from_archive[k] = (char)BitReaderReadBits(reader, 8);
        }
        filename_from_archive[filename_len] = '\0';

        uint64_t original_file_size_bytes = BitReaderReadUint64(reader);

        printf("\nProcessing archive entry %u/%u: %s (Original size: %llu bytes)\n",
               file_idx + 1, num_total_files, filename_from_archive, (unsigned long long)original_file_size_bytes);

        uint16_t huff_table_entry_count = BitReaderReadBits(reader, 16);
        DecodingTreeNode *decoding_root = CreateDecodingNode();
        if (!decoding_root)
        {
            perror(COLOR_STR("Failed to create decoding tree root", RED));
            free(filename_from_archive);
            BitReaderClose(reader);
            return 1;
        }

        int huff_table_valid = 1;
        for (uint16_t entry_idx = 0; entry_idx < huff_table_entry_count; ++entry_idx)
        {
            uint16_t symbol = 0;
            if (symbol_size_val == 1)
            {
                symbol = (uint16_t)BitReaderReadBits(reader, 8);
            }
            else
            {
                symbol = (uint16_t)BitReaderReadBits(reader, 16);
            }
            uint8_t code_len = BitReaderReadBits(reader, 8);

            if (code_len > 64 && huff_table_entry_count > 1)
            {
                fprintf(stderr, COLOR_STR("Error: Invalid code_len (%u) for symbol %u in %s.\n", RED), code_len, symbol, filename_from_archive);
                huff_table_valid = 0;
                break;
            }
            uint64_t code = 0;
            if (code_len > 0)
                code = BitReaderReadBits(reader, code_len);

            if (!InsertIntoDecodingTree(decoding_root, symbol, code, code_len))
            {
                fprintf(stderr, COLOR_STR("Error inserting into decoding tree for %s.\n", RED), filename_from_archive);
                huff_table_valid = 0;
                break;
            }
        }
        if (!huff_table_valid)
        {
            FreeDecodingTree(decoding_root);
            free(filename_from_archive);
            BitReaderClose(reader);
            return 1;
        }

        int should_extract = extractAll;
        if (!extractAll && wantedCount > 0)
        {
            should_extract = 0;
            for (size_t w_idx = 0; w_idx < wantedCount; ++w_idx)
            {
                if (strcmp(wantedFiles[w_idx], filename_from_archive) == 0)
                {
                    should_extract = 1;
                    break;
                }
            }
        }

        FILE *outFile = NULL;
        char full_output_path[PATH_MAX];
        int opened_successfully_for_writing = 0;
        if (should_extract)
        {
            snprintf(full_output_path, PATH_MAX, "%s/%s", outputDir, filename_from_archive);
            full_output_path[PATH_MAX - 1] = '\0';

            char dir_part_to_create[PATH_MAX];
            strncpy(dir_part_to_create, full_output_path, PATH_MAX - 1);
            dir_part_to_create[PATH_MAX - 1] = '\0';

            char *last_slash = strrchr(dir_part_to_create, '/');
            if (last_slash != NULL && last_slash != dir_part_to_create)
            {
                *last_slash = '\0';
                if (strlen(dir_part_to_create) > 0 && CreateDirectoryRecursive(dir_part_to_create) != 0)
                {
                    fprintf(stderr, COLOR_STR("Warning: Could not create directory %s for storing %s (errno: %d, %s)\n", RED),
                            dir_part_to_create, filename_from_archive, errno, strerror(errno));
                }
            }

            outFile = fopen(full_output_path, "wb");
            if (!outFile)
            {
                perror(COLOR_STR("Error opening output file for writing", RED));
                fprintf(stderr, COLOR_STR("Failed output file: %s\n", RED), full_output_path);
                should_extract = 0; // Не можем извлечь, если не открылся файл
            }
            else
            {
                printf("  Extracting to: %s\n", full_output_path);
                opened_successfully_for_writing = 1;
            }
        }
        else
            printf("  Skipping file: %s\n", filename_from_archive);

        uint64_t bytes_written_or_skipped = 0;
        DecodingTreeNode *current_node = decoding_root;
        int error_occurred_for_this_file = 0;

        while (bytes_written_or_skipped < original_file_size_bytes)
        {
            while (!current_node->is_leaf)
            {
                int bit = BitReaderReadBit(reader);
                if (bit == -1)
                {
                    if (bytes_written_or_skipped < original_file_size_bytes && original_file_size_bytes > 0)
                    {
                         fprintf(stderr, COLOR_STR("\nError: Unexpected end of archive data while decompressing %s. File may be incomplete (%llu/%llu processed).\n", RED),
                                filename_from_archive, bytes_written_or_skipped, original_file_size_bytes);
                    }
                    error_occurred_for_this_file = 1;
                    goto end_of_current_file_processing;
                }

                current_node = (bit == 0) ? current_node->child0 : current_node->child1;

                if (current_node == NULL)
                {
                    fprintf(stderr, COLOR_STR("\nError: Invalid Huffman code sequence in archive for %s. Corrupted data.\n", RED), filename_from_archive);
                    error_occurred_for_this_file = 1;
                    goto end_of_current_file_processing;
                }
            }

            uint16_t decoded_symbol_value = current_node->symbol;

            if (should_extract && outFile)
            {
                if (symbol_size_val == 1)
                {
                    uint8_t sym_byte_to_write = (uint8_t)decoded_symbol_value;
                    if (fwrite(&sym_byte_to_write, 1, 1, outFile) != 1)
                    {
                        perror(COLOR_STR("Error writing byte to output file", RED));
                        error_occurred_for_this_file = 1;
                        goto end_of_current_file_processing;
                    }
                    bytes_written_or_skipped++;
                }
                else if (symbol_size_val == 2)
                {
                    unsigned char s_bytes[2];
                    s_bytes[0] = (unsigned char)(decoded_symbol_value >> 8);
                    s_bytes[1] = (unsigned char)(decoded_symbol_value & 0xFF);

                    if (bytes_written_or_skipped < original_file_size_bytes)
                    {
                        if (fwrite(&s_bytes[0], 1, 1, outFile) != 1)
                        {
                            perror(COLOR_STR("Error writing first byte of 2-byte symbol", RED));
                            error_occurred_for_this_file = 1; goto end_of_current_file_processing;
                        }
                        bytes_written_or_skipped++;
                    }

                    if (bytes_written_or_skipped < original_file_size_bytes)
                    {
                        if (fwrite(&s_bytes[1], 1, 1, outFile) != 1)
                        {
                            perror(COLOR_STR("Error writing second byte of 2-byte symbol", RED));
                            error_occurred_for_this_file = 1; goto end_of_current_file_processing;
                        }
                        bytes_written_or_skipped++;
                    }
                }
            }
            else
            {
                if (symbol_size_val == 1)
                    if (bytes_written_or_skipped < original_file_size_bytes)
                        bytes_written_or_skipped++;
                else if (symbol_size_val == 2)
                {
                    if (bytes_written_or_skipped < original_file_size_bytes)
                        bytes_written_or_skipped++; // Для первого "логического" байта символа
                    if (bytes_written_or_skipped < original_file_size_bytes)
                        bytes_written_or_skipped++; // Для второго "логического" байта символа
                }
            }
            
            current_node = decoding_root; // Сбрасываем на корень для следующего символа

            // Обновление индикатора прогресса
            if (opened_successfully_for_writing && (bytes_written_or_skipped % 102400 == 0 || bytes_written_or_skipped == original_file_size_bytes))
            {
                printf("\r  Decompressing %s: %llu / %llu bytes (%.2f%%)",
                       filename_from_archive, (unsigned long long)bytes_written_or_skipped,
                       (unsigned long long)original_file_size_bytes,
                       original_file_size_bytes > 0 ? (double)bytes_written_or_skipped * 100.0 / original_file_size_bytes : 100.0);
                fflush(stdout);
            }
        }

    end_of_current_file_processing:; // Метка для выхода из цикла обработки содержимого файла

        if (opened_successfully_for_writing)
             printf("\n");
        if (outFile)
        {
            fclose(outFile);
            outFile = NULL;
        }

        FreeDecodingTree(decoding_root);
        free(filename_from_archive);

        if (error_occurred_for_this_file && file_idx < num_total_files - 1) {
            fprintf(stderr, COLOR_STR("Warning: An error occurred processing file %s. Subsequent files may be affected.\n", YELLOW), filename_from_archive);
            BitReaderClose(reader);
        }
    }
    BitReaderClose(reader);
    printf(COLOR_STR("\nDecompression finished.\n", GREEN));
    return 0;
}