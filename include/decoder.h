#ifndef DECODER_H
#define DECODER_H

#include <stddef.h>
#include <stdint.h>

int DecodeArchive(const char *archivePath, const char *outputDir, const char **wantedFiles, size_t wantedCount, int extractAll);

#endif