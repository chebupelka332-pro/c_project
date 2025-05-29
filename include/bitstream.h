#ifndef BITSTREAM_H
#define BITSTREAM_H

#include <stdio.h>
#include <stddef.h>

// Поток для побитовой записи
typedef struct 
{
    FILE *file;
    unsigned char buffer;
    int bitPos; // от 0 до 7
} BitWriter;

// Поток для побитового чтения
typedef struct
{
    FILE *file;
    unsigned char buffer;
    int bitPos; // от 0 до 7
} BitReader;

// --- BitWriter ---

BitWriter *BitWriterOpen(const char *path);
void BitWriterWriteBit(BitWriter *writer, int bit);
void BitWriterWriteBits(BitWriter *writer, unsigned int value, int count);
void BitWriterFlush(BitWriter *writer);
void BitWriterClose(BitWriter *writer);

// --- BitReader ---

BitReader *BitReaderOpen(const char *path);
int BitReaderReadBit(BitReader *reader);
unsigned int BitReaderReadBits(BitReader *reader, int count);
void BitReaderClose(BitReader *reader);

#endif
