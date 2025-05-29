#ifndef ENCODER_H
#define ENCODER_H

#include <stddef.h>
#include <stdint.h>
#include "args.h"

int EncodeFiles(ParsedArgs *args, const char **inputPaths, size_t numInputPaths, const char *outputPath, uint32_t symbol_size);

#endif