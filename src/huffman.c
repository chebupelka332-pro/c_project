#include "huffman.h"
#include <color.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <stdio.h>

#define MAX_SYMBOLS_1B 256
#define MAX_SYMBOLS_2B 65536
#define PADDING_BYTE 0x00

typedef struct HuffNode
{
    uint64_t freq;
    uint16_t symbol;
    struct HuffNode *left, *right;
} HuffNode;

typedef struct
{
    HuffNode **data;
    size_t size;
    size_t capacity;
} MinHeap;

static MinHeap *CreateMinHeap(size_t capacity)
{
    MinHeap *heap = malloc(sizeof(MinHeap));
    if (!heap)
        return NULL;
    heap->data = malloc(capacity * sizeof(HuffNode *));
    if (!heap->data)
    {
        free(heap);
        return NULL;
    }
    heap->size = 0;
    heap->capacity = capacity;
    return heap;
}

static void SwapNodes(HuffNode **a, HuffNode **b)
{
    HuffNode *tmp = *a;
    *a = *b;
    *b = tmp;
}

static void HeapifyDown(MinHeap *heap, size_t i)
{
    size_t smallest = i, left = 2 * i + 1, right = 2 * i + 2;
    if (left < heap->size && heap->data[left]->freq < heap->data[smallest]->freq)
        smallest = left;
    if (right < heap->size && heap->data[right]->freq < heap->data[smallest]->freq)
        smallest = right;
    if (smallest != i)
    {
        SwapNodes(&heap->data[i], &heap->data[smallest]);
        HeapifyDown(heap, smallest);
    }
}

static void HeapifyUp(MinHeap *heap, size_t i)
{
    while (i && heap->data[i]->freq < heap->data[(i - 1) / 2]->freq)
    {
        SwapNodes(&heap->data[i], &heap->data[(i - 1) / 2]);
        i = (i - 1) / 2;
    }
}

static void PushHeap(MinHeap *heap, HuffNode *node)
{
    if (heap->size == heap->capacity)
    {
        return;
    }
    heap->data[heap->size++] = node;
    HeapifyUp(heap, heap->size - 1);
}

static HuffNode *PopHeap(MinHeap *heap)
{
    if (heap->size == 0)
        return NULL;
    HuffNode *min = heap->data[0];
    heap->data[0] = heap->data[--heap->size];
    if (heap->size > 0)
    {
        HeapifyDown(heap, 0);
    }
    return min;
}

static void FreeTree(HuffNode *root)
{
    if (!root)
        return;
    FreeTree(root->left);
    FreeTree(root->right);
    free(root);
}

static void BuildCodes(HuffNode *node, HuffCode *table, uint64_t code, uint32_t length)
{
    if (!node)
        return;

    if (!node->left && !node->right)
    {
        if (length > sizeof(uint64_t) * 8)
        {
            fprintf(stderr, COLOR_STR("Warning: Huffman code length %u for symbol %u exceeds 64 bits!\n", YELLOW), length, node->symbol);
            table[node->symbol].code_len = 0;
            table[node->symbol].code = 0;
            return;
        }
        if (length == 0)
        {
            table[node->symbol].code = 0;
            table[node->symbol].code_len = 0;
        }
        else
        {
            table[node->symbol].code = code;
            table[node->symbol].code_len = length;
        }
        return;
    }
    if (node->left)
        BuildCodes(node->left, table, (code << 1), length + 1);
    if (node->right)
        BuildCodes(node->right, table, (code << 1) | 1, length + 1);
}

HuffCode *GenerateCodes(FILE *data, uint64_t file_size, uint32_t symbol_size)
{
    if (symbol_size != 1 && symbol_size != 2)
        return NULL;

    uint64_t symbol_count = (symbol_size == 1) ? MAX_SYMBOLS_1B : MAX_SYMBOLS_2B;
    uint64_t *freq_table = calloc(symbol_count, sizeof(uint64_t));
    if (!freq_table)
        return NULL;

    if (file_size > 0)
    {
        uint64_t bytes_counted_from_file = 0;
        int c1, c2;

        while (bytes_counted_from_file < file_size)
        {
            uint16_t symbol_to_count;

            if (symbol_size == 1)
            {
                c1 = fgetc(data);
                if (c1 == EOF)
                {
                    if (ferror(data))
                    {
                        perror(COLOR_STR("Error reading file for frequency count", RED));
                        free(freq_table);
                        return NULL;
                    }
                    break;
                }
                symbol_to_count = (uint8_t)c1;
                bytes_counted_from_file += 1;
            }
            else
            {
                c1 = fgetc(data);
                if (c1 == EOF)
                {
                    if (ferror(data))
                    {
                        perror(COLOR_STR("Error reading file for frequency count", RED));
                        free(freq_table);
                        return NULL;
                    }
                    break;
                }
                bytes_counted_from_file += 1;

                if (bytes_counted_from_file < file_size)
                {
                    c2 = fgetc(data);
                    if (c2 == EOF)
                    {
                        if (ferror(data))
                        {
                            perror(COLOR_STR("Error reading file for frequency count", RED));
                            free(freq_table);
                            return NULL;
                        }
                        fprintf(stderr, COLOR_STR("Warning: Unexpected EOF when reading second byte of a pair in GenerateCodes. Padding.\n", YELLOW));
                        symbol_to_count = ((uint16_t)c1 << 8) | PADDING_BYTE;
                    }
                    else
                    {
                        symbol_to_count = ((uint16_t)c1 << 8) | (uint8_t)c2;
                        bytes_counted_from_file += 1;
                    }
                }
                else
                {
                    symbol_to_count = ((uint16_t)c1 << 8) | PADDING_BYTE;
                }
            }
            freq_table[symbol_to_count]++;

            if (bytes_counted_from_file % 204800 == 0 || bytes_counted_from_file == file_size)
            {
                printf("\r  Building alphabet: %llu / %llu bytes (%.2f%%)",
                       (unsigned long long)bytes_counted_from_file,
                       (unsigned long long)file_size,
                       (file_size > 0 ? (double)bytes_counted_from_file * 100.0 / file_size : 0.0));
                fflush(stdout);
            }
        }
        printf("\n");

        if (fseek(data, 0, SEEK_SET) != 0)
        {
            perror(COLOR_STR("GenerateCodes: Failed to rewind input file stream after frequency counting", RED));
            free(freq_table);
            return NULL;
        }
    }

    MinHeap *heap = CreateMinHeap(symbol_count);
    if (!heap)
    {
        free(freq_table);
        return NULL;
    }

    int actual_symbol_count_in_heap = 0;
    for (uint64_t i = 0; i < symbol_count; ++i)
    {
        if (freq_table[i])
        {
            HuffNode *node = malloc(sizeof(HuffNode));
            if (!node)
            {
                free(freq_table);
                free(heap->data);
                free(heap);
                return NULL;
            }
            node->freq = freq_table[i];
            node->symbol = (uint16_t)i;
            node->left = node->right = NULL;
            PushHeap(heap, node);
            actual_symbol_count_in_heap++;
        }
    }
    free(freq_table);

    while (heap->size > 1)
    {
        HuffNode *a = PopHeap(heap);
        HuffNode *b = PopHeap(heap);
        if (!a || !b)
        {
            FreeTree(a);
            FreeTree(b);
            free(heap->data);
            free(heap);
            return NULL;
        }
        HuffNode *parent = malloc(sizeof(HuffNode));
        if (!parent)
        {
            FreeTree(a);
            FreeTree(b);
            free(heap->data);
            free(heap);
            return NULL;
        }
        parent->freq = a->freq + b->freq;
        parent->symbol = 0;
        parent->left = a;
        parent->right = b;
        PushHeap(heap, parent);
    }

        HuffNode *root = PopHeap(heap);
    HuffCode *table = calloc(symbol_count, sizeof(HuffCode));
    if (!table)
    {
        FreeTree(root);
        if (heap)
        {
            free(heap->data);
            free(heap);
        }
        return NULL;
    }

    if (root)
    {
        // Если файл не пуст и в нем только один уникальный символ
        if (!root->left && !root->right && actual_symbol_count_in_heap == 1 && file_size > 0)
        {
            table[root->symbol].code = 0;
            table[root->symbol].code_len = 1;
        }
        else if (file_size > 0 || actual_symbol_count_in_heap > 0)
            BuildCodes(root, table, 0, 0);
    }

    FreeTree(root);
    if (heap)
    {
        free(heap->data);
        free(heap);
    }
    return table;
}