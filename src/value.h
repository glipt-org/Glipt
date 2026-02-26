#ifndef glipt_value_h
#define glipt_value_h

#include "common.h"

typedef struct Obj Obj;
typedef struct ObjString ObjString;

// ---- NaN Boxing ----
// Values are encoded in 64 bits using IEEE 754 NaN bit patterns.
// Regular doubles are stored as-is. Other types use quiet NaN + tag bits.

typedef uint64_t Value;

#define SIGN_BIT ((uint64_t)0x8000000000000000)
#define QNAN     ((uint64_t)0x7FFC000000000000)

#define TAG_NIL   1
#define TAG_FALSE 2
#define TAG_TRUE  3

// Wrap
#define NIL_VAL         ((Value)(QNAN | TAG_NIL))
#define TRUE_VAL        ((Value)(QNAN | TAG_TRUE))
#define FALSE_VAL       ((Value)(QNAN | TAG_FALSE))
#define BOOL_VAL(b)     ((b) ? TRUE_VAL : FALSE_VAL)
#define NUMBER_VAL(num) numToValue(num)
#define OBJ_VAL(obj)    (Value)(SIGN_BIT | QNAN | (uint64_t)(uintptr_t)(obj))

// Type checks
#define IS_NIL(v)    ((v) == NIL_VAL)
#define IS_BOOL(v)   (((v) == TRUE_VAL) || ((v) == FALSE_VAL))
#define IS_NUMBER(v) (((v) & QNAN) != QNAN)
#define IS_OBJ(v)    (((v) & (SIGN_BIT | QNAN)) == (SIGN_BIT | QNAN))

// Unwrap
#define AS_BOOL(v)   ((v) == TRUE_VAL)
#define AS_NUMBER(v) valueToNum(v)
#define AS_OBJ(v)    ((Obj*)(uintptr_t)((v) & ~(SIGN_BIT | QNAN)))

// Double <-> uint64 conversion via memcpy (no UB, optimizer sees through it)
static inline Value numToValue(double num) {
    Value v;
    memcpy(&v, &num, sizeof(double));
    return v;
}

static inline double valueToNum(Value v) {
    double num;
    memcpy(&num, &v, sizeof(double));
    return num;
}

// Inline falsey check (hot path: JUMP_IF_FALSE)
static inline bool isFalsey(Value value) {
    if (IS_NIL(value)) return true;
    if (IS_BOOL(value)) return value == FALSE_VAL;
    if (IS_NUMBER(value)) return AS_NUMBER(value) == 0;
    return false;
}

// Dynamic array of values (used for constant pools, etc.)
typedef struct {
    int capacity;
    int count;
    Value* values;
} ValueArray;

void initValueArray(ValueArray* array);
void writeValueArray(ValueArray* array, Value value);
void freeValueArray(ValueArray* array);
void printValue(Value value);
bool valuesEqual(Value a, Value b);

#endif
