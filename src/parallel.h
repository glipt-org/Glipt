#ifndef glipt_parallel_h
#define glipt_parallel_h

#include "common.h"
#include "value.h"

// Forward declare
typedef struct VM VM;

// Native function: run a list of exec commands in parallel
// Takes a list of command strings, returns a list of result maps
Value parallelExec(VM* vm, int argCount, Value* args);

#endif
