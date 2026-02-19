#ifndef glipt_chunk_h
#define glipt_chunk_h

#include "common.h"
#include "value.h"
#include "opcode.h"
#include "table.h"

typedef struct {
    int count;
    int capacity;
    uint8_t* code;
    int* lines;         // source line for each byte
    ValueArray constants;
    Table constantIndex; // string constant dedup (ObjString* -> index)
} Chunk;

void initChunk(Chunk* chunk);
void freeChunk(Chunk* chunk);
void writeChunk(Chunk* chunk, uint8_t byte, int line);
int addConstant(Chunk* chunk, Value value);

#endif
