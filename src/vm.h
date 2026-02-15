#ifndef glipt_vm_h
#define glipt_vm_h

#include "common.h"
#include "value.h"
#include "chunk.h"
#include "table.h"
#include "object.h"
#include "permission.h"

#define FRAMES_MAX 256
#define STACK_MAX (FRAMES_MAX * UINT8_COUNT)
#define HANDLER_MAX 64

typedef struct {
    ObjClosure* closure;
    uint8_t* ip;
    Value* slots;
} CallFrame;

// Error handler for on failure blocks
typedef struct {
    uint8_t* handlerIP;     // IP of handler code
    int frameCount;         // frame count when handler was pushed
    Value* stackTop;        // stack top when handler was pushed
    int scopeDepth;         // scope depth for cleanup
} ErrorHandler;

struct VM {
    CallFrame frames[FRAMES_MAX];
    int frameCount;

    Value stack[STACK_MAX];
    Value* stackTop;

    Table globals;
    Table strings;

    ObjUpvalue* openUpvalues;

    // GC state
    Obj* objects;
    int grayCount;
    int grayCapacity;
    Obj** grayStack;
    size_t bytesAllocated;
    size_t nextGC;

    // Permission system
    PermissionSet permissions;

    // Error handler stack
    ErrorHandler handlers[HANDLER_MAX];
    int handlerCount;

    // Pending error (for handler dispatch)
    bool hasError;
    Value currentError;     // error map: {message, type, exitCode, ...}
};

typedef enum {
    INTERPRET_OK,
    INTERPRET_COMPILE_ERROR,
    INTERPRET_RUNTIME_ERROR,
} InterpretResult;

void initVM(VM* vm);
void freeVM(VM* vm);
InterpretResult interpret(VM* vm, const char* source);
void defineNative(VM* vm, const char* name, NativeFn function, int arity);

// Used by memory.c
void setCurrentVM(VM* vm);

#endif
