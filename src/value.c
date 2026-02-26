#include "value.h"
#include "object.h"
#include "memory.h"

void initValueArray(ValueArray* array) {
    array->values = NULL;
    array->capacity = 0;
    array->count = 0;
}

void writeValueArray(ValueArray* array, Value value) {
    if (array->capacity < array->count + 1) {
        int oldCapacity = array->capacity;
        array->capacity = oldCapacity < 8 ? 8 : oldCapacity * 2;
        array->values = (Value*)reallocate(array->values,
            sizeof(Value) * oldCapacity,
            sizeof(Value) * array->capacity);
    }
    array->values[array->count] = value;
    array->count++;
}

void freeValueArray(ValueArray* array) {
    reallocate(array->values, sizeof(Value) * array->capacity, 0);
    initValueArray(array);
}

void printValue(Value value) {
    if (IS_BOOL(value)) {
        printf(AS_BOOL(value) ? "true" : "false");
    } else if (IS_NIL(value)) {
        printf("nil");
    } else if (IS_NUMBER(value)) {
        double num = AS_NUMBER(value);
        if (num == (int)num) {
            printf("%d", (int)num);
        } else {
            printf("%g", num);
        }
    } else if (IS_OBJ(value)) {
        printObject(value);
    }
}

bool valuesEqual(Value a, Value b) {
    // With NaN boxing, bit equality works for nil, bool, and interned strings.
    // Special case: NaN != NaN per IEEE 754.
    if (IS_NUMBER(a) && IS_NUMBER(b)) {
        return AS_NUMBER(a) == AS_NUMBER(b);
    }
    return a == b;
}
