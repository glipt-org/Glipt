#ifndef glipt_dataformat_h
#define glipt_dataformat_h

#include "common.h"
#include "value.h"

// Forward declare
typedef struct VM VM;

// Parse a JSON string into Glipt values (maps, lists, strings, numbers, bools, nil)
// Returns a Value. On error, returns NIL_VAL and prints an error message.
Value parseJSON(VM* vm, const char* json, int length);

// Serialize a Glipt value to a JSON string.
// Returns an ObjString*.
Value toJSON(VM* vm, Value value);

#endif
