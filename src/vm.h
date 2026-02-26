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
#define GLOBAL_IC_SIZE 512

typedef struct {
    ObjString* key;
    Entry* entry;
    int tableCapacity;
} GlobalICSlot;

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

    // Global variable inline cache
    GlobalICSlot globalIC[GLOBAL_IC_SIZE];

    // For calling closures from native functions (run() returns when
    // frameCount drops to baseFrameCount instead of 0)
    int baseFrameCount;

    // Script arguments (set by main, read by sys.args)
    int scriptArgc;
    char** scriptArgv;

    // Import system
    Table modules;          // path -> ObjMap (cached module exports)
    const char* scriptPath; // path of main script (for relative import resolution)
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

// Helpers exposed for module implementations
void vmPush(VM* vm, Value value);
Value vmPop(VM* vm);
void vmRaiseError(VM* vm, const char* message, const char* type);
void defineModuleNative(VM* vm, ObjMap* module, const char* name,
                        NativeFn function, int arity);

// Used by memory.c
void setCurrentVM(VM* vm);

#endif
