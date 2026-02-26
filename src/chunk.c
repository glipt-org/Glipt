#include "chunk.h"
#include "memory.h"
#include "object.h"

void initChunk(Chunk* chunk) {
    chunk->count = 0;
    chunk->capacity = 0;
    chunk->code = NULL;
    chunk->lines = NULL;
    initValueArray(&chunk->constants);
    initTable(&chunk->constantIndex);
}

void freeChunk(Chunk* chunk) {
    FREE_ARRAY(uint8_t, chunk->code, chunk->capacity);
    FREE_ARRAY(int, chunk->lines, chunk->capacity);
    freeValueArray(&chunk->constants);
    freeTable(&chunk->constantIndex);
    initChunk(chunk);
}

void writeChunk(Chunk* chunk, uint8_t byte, int line) {
    if (chunk->capacity < chunk->count + 1) {
        int oldCapacity = chunk->capacity;
        chunk->capacity = GROW_CAPACITY(oldCapacity);
        chunk->code = GROW_ARRAY(uint8_t, chunk->code, oldCapacity, chunk->capacity);
        chunk->lines = GROW_ARRAY(int, chunk->lines, oldCapacity, chunk->capacity);
    }

    chunk->code[chunk->count] = byte;
    chunk->lines[chunk->count] = line;
    chunk->count++;
}

int addConstant(Chunk* chunk, Value value) {
    // O(1) dedup for string constants via hash table
    if (IS_OBJ(value) && IS_STRING(value)) {
        Value existing;
        if (tableGet(&chunk->constantIndex, AS_STRING(value), &existing)) {
            return (int)AS_NUMBER(existing);
        }
        writeValueArray(&chunk->constants, value);
        int index = chunk->constants.count - 1;
        tableSet(&chunk->constantIndex, AS_STRING(value), NUMBER_VAL(index));
        return index;
    }

    // Linear scan for non-strings (numbers are few)
    for (int i = 0; i < chunk->constants.count; i++) {
        if (valuesEqual(chunk->constants.values[i], value)) return i;
    }
    writeValueArray(&chunk->constants, value);
    return chunk->constants.count - 1;
}
