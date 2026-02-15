#ifndef glipt_chunk_h
#define glipt_chunk_h

#include "common.h"
#include "value.h"
#include "opcode.h"

typedef struct {
    int count;
    int capacity;
    uint8_t* code;
    int* lines;         // source line for each byte
    ValueArray constants;
} Chunk;

void initChunk(Chunk* chunk);
void freeChunk(Chunk* chunk);
void writeChunk(Chunk* chunk, uint8_t byte, int line);
int addConstant(Chunk* chunk, Value value);

#endif
