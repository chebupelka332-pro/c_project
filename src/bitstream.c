#include "bitstream.h"
#include <stdlib.h>


BitWriter *BitWriterOpen(const char *path)
{
    BitWriter *writer = malloc(sizeof(BitWriter));

    if (!writer)
        return NULL;

    writer->file = fopen(path, "wb");
    if (!writer->file)
    {
        free(writer);
        return NULL;
    }

    writer->buffer = 0;
    writer->bitPos = 0;
    return writer;
}

void BitWriterWriteBit(BitWriter *writer, int bit)
{
    if (bit)
        writer->buffer |= (1 << (7 - writer->bitPos));
        
    writer->bitPos++;

    if (writer->bitPos == 8)
    {
        fwrite(&writer->buffer, 1, 1, writer->file);
        writer->buffer = 0;
        writer->bitPos = 0;
    }
}

void BitWriterWriteBits(BitWriter *writer, unsigned int value, int count)
{
    for (int i = count - 1; i >= 0; i--)
        BitWriterWriteBit(writer, (value >> i) & 1);
}

void BitWriterFlush(BitWriter *writer)
{
    if (writer->bitPos > 0)
        fwrite(&writer->buffer, 1, 1, writer->file);

    writer->buffer = 0;
    writer->bitPos = 0;
}

void BitWriterClose(BitWriter *writer)
{
    if (!writer)
        return;
    BitWriterFlush(writer);
    fclose(writer->file);
    free(writer);
}

// --- BitReader ---

BitReader *BitReaderOpen(const char *path)
{
    BitReader *reader = malloc(sizeof(BitReader));

    if (!reader)
        return NULL;

    reader->file = fopen(path, "rb");

    if (!reader->file)
    {
        free(reader);
        return NULL;
    }

    reader->bitPos = 8; // Чтобы сразу считать байт при первом чтении
    return reader;
}

int BitReaderReadBit(BitReader *reader)
{
    if (reader->bitPos == 8)
    {
        if (fread(&reader->buffer, 1, 1, reader->file) != 1)
            return -1; // EOF
        reader->bitPos = 0;
    }

    int bit = (reader->buffer >> (7 - reader->bitPos)) & 1;
    reader->bitPos++;
    return bit;
}

unsigned int BitReaderReadBits(BitReader *reader, int count)
{
    unsigned int result = 0;
    for (int i = 0; i < count; i++)
    {
        int bit = BitReaderReadBit(reader);
        if (bit == -1)
            break; // EOF
        result = (result << 1) | bit;
    }
    return result;
}

void BitReaderClose(BitReader *reader)
{
    if (!reader)
        return;
    fclose(reader->file);
    free(reader);
}
