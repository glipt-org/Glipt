#ifndef glipt_object_h
#define glipt_object_h

#include "common.h"
#include "value.h"
#include "chunk.h"
#include "table.h"

typedef enum {
    OBJ_STRING,
    OBJ_FUNCTION,
    OBJ_CLOSURE,
    OBJ_UPVALUE,
    OBJ_NATIVE,
    OBJ_LIST,
    OBJ_MAP,
} ObjType;

struct Obj {
    ObjType type;
    bool isMarked;
    struct Obj* next;
};

// Type checks
#define OBJ_TYPE(value)       (AS_OBJ(value)->type)
#define IS_STRING(value)      isObjType(value, OBJ_STRING)
#define IS_FUNCTION(value)    isObjType(value, OBJ_FUNCTION)
#define IS_CLOSURE(value)     isObjType(value, OBJ_CLOSURE)
#define IS_NATIVE(value)      isObjType(value, OBJ_NATIVE)
#define IS_LIST(value)        isObjType(value, OBJ_LIST)
#define IS_MAP(value)         isObjType(value, OBJ_MAP)

// Unwrap
#define AS_STRING(value)      ((ObjString*)AS_OBJ(value))
#define AS_CSTRING(value)     (((ObjString*)AS_OBJ(value))->chars)
#define AS_FUNCTION(value)    ((ObjFunction*)AS_OBJ(value))
#define AS_CLOSURE(value)     ((ObjClosure*)AS_OBJ(value))
#define AS_NATIVE(value)      ((ObjNative*)AS_OBJ(value))
#define AS_LIST(value)        ((ObjList*)AS_OBJ(value))
#define AS_MAP(value)         ((ObjMap*)AS_OBJ(value))

static inline bool isObjType(Value value, ObjType type) {
    return IS_OBJ(value) && AS_OBJ(value)->type == type;
}

// ---- String ----
struct ObjString {
    Obj obj;
    int length;
    uint32_t hash;
    char chars[];   // flexible array member
};

// ---- Function ----
typedef struct {
    Obj obj;
    int arity;
    int upvalueCount;
    Chunk chunk;
    ObjString* name;
} ObjFunction;

// ---- Upvalue ----
typedef struct ObjUpvalue {
    Obj obj;
    Value* location;
    Value closed;
    struct ObjUpvalue* next;
} ObjUpvalue;

// ---- Closure ----
typedef struct {
    Obj obj;
    ObjFunction* function;
    ObjUpvalue** upvalues;
    int upvalueCount;
} ObjClosure;

// ---- Native Function ----
typedef struct VM VM;
typedef Value (*NativeFn)(VM* vm, int argCount, Value* args);

typedef struct {
    Obj obj;
    NativeFn function;
    const char* name;
    int arity;  // -1 for variadic
} ObjNative;

// ---- List ----
typedef struct {
    Obj obj;
    int count;
    int capacity;
    Value* items;
} ObjList;

// ---- Map ----
typedef struct {
    Obj obj;
    Table table;
} ObjMap;

// ---- Constructors ----
ObjString* copyString(VM* vm, const char* chars, int length);
ObjString* takeString(VM* vm, char* chars, int length);
ObjFunction* newFunction(VM* vm);
ObjUpvalue* newUpvalue(VM* vm, Value* slot);
ObjClosure* newClosure(VM* vm, ObjFunction* function);
ObjNative* newNative(VM* vm, NativeFn function, const char* name, int arity);
ObjList* newList(VM* vm);
ObjMap* newMap(VM* vm);

// ---- Operations ----
void listAppend(VM* vm, ObjList* list, Value value);
void printObject(Value value);
void freeObject(Obj* object);
void markObject(Obj* object);

#endif
