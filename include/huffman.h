#ifndef HUFFMAN_H
#define HUFFMAN_H

#include <stdint.h>
#include <stdio.h>

typedef struct HuffCode
{
    uint64_t code;
    uint32_t code_len;
} HuffCode;

HuffCode* GenerateCodes(FILE *data, uint64_t file_size, uint32_t symbol_size);

#endif