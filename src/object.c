#include "object.h"
#include "memory.h"
#include "table.h"
#include "vm.h"

static Obj* allocateObject(VM* vm, size_t size, ObjType type) {
    Obj* object = (Obj*)reallocate(NULL, 0, size);
    object->type = type;
    object->isMarked = false;
    object->next = vm->objects;
    vm->objects = object;
    return object;
}

#define ALLOCATE_OBJ(vm, type, objectType) \
    (type*)allocateObject(vm, sizeof(type), objectType)

// ---- String ----

static uint32_t hashString(const char* key, int length) {
    uint32_t hash = 2166136261u;
    for (int i = 0; i < length; i++) {
        hash ^= (uint8_t)key[i];
        hash *= 16777619;
    }
    return hash;
}

static ObjString* allocateString(VM* vm, const char* chars, int length,
                                  uint32_t hash) {
    ObjString* string = (ObjString*)allocateObject(vm,
        sizeof(ObjString) + length + 1, OBJ_STRING);
    string->length = length;
    string->hash = hash;
    memcpy(string->chars, chars, length);
    string->chars[length] = '\0';

    // Protect from GC during intern table resize
    vmPush(vm, OBJ_VAL(string));
    tableSet(&vm->strings, string, NIL_VAL);
    vmPop(vm);

    return string;
}

ObjString* copyString(VM* vm, const char* chars, int length) {
    uint32_t hash = hashString(chars, length);
    ObjString* interned = tableFindString(&vm->strings, chars, length, hash);
    if (interned != NULL) return interned;

    return allocateString(vm, chars, length, hash);
}

ObjString* takeString(VM* vm, char* chars, int length) {
    uint32_t hash = hashString(chars, length);
    ObjString* interned = tableFindString(&vm->strings, chars, length, hash);
    if (interned != NULL) {
        FREE_ARRAY(char, chars, length + 1);
        return interned;
    }

    ObjString* string = allocateString(vm, chars, length, hash);
    FREE_ARRAY(char, chars, length + 1);
    return string;
}

// ---- Function ----

ObjFunction* newFunction(VM* vm) {
    ObjFunction* function = ALLOCATE_OBJ(vm, ObjFunction, OBJ_FUNCTION);
    function->arity = 0;
    function->upvalueCount = 0;
    function->name = NULL;
    initChunk(&function->chunk);
    return function;
}

// ---- Upvalue ----

ObjUpvalue* newUpvalue(VM* vm, Value* slot) {
    ObjUpvalue* upvalue = ALLOCATE_OBJ(vm, ObjUpvalue, OBJ_UPVALUE);
    upvalue->location = slot;
    upvalue->closed = NIL_VAL;
    upvalue->next = NULL;
    return upvalue;
}

// ---- Closure ----

ObjClosure* newClosure(VM* vm, ObjFunction* function) {
    ObjUpvalue** upvalues = ALLOCATE(ObjUpvalue*, function->upvalueCount);
    for (int i = 0; i < function->upvalueCount; i++) {
        upvalues[i] = NULL;
    }

    ObjClosure* closure = ALLOCATE_OBJ(vm, ObjClosure, OBJ_CLOSURE);
    closure->function = function;
    closure->upvalues = upvalues;
    closure->upvalueCount = function->upvalueCount;
    return closure;
}

// ---- Native ----

ObjNative* newNative(VM* vm, NativeFn function, const char* name, int arity) {
    ObjNative* native = ALLOCATE_OBJ(vm, ObjNative, OBJ_NATIVE);
    native->function = function;
    native->name = name;
    native->arity = arity;
    return native;
}

// ---- List ----

ObjList* newList(VM* vm) {
    ObjList* list = ALLOCATE_OBJ(vm, ObjList, OBJ_LIST);
    list->count = 0;
    list->capacity = 0;
    list->items = NULL;
    return list;
}

void listAppend(VM* vm, ObjList* list, Value value) {
    (void)vm; // Will be used for GC coordination
    if (list->capacity < list->count + 1) {
        int oldCapacity = list->capacity;
        list->capacity = GROW_CAPACITY(oldCapacity);
        list->items = GROW_ARRAY(Value, list->items, oldCapacity, list->capacity);
    }
    list->items[list->count] = value;
    list->count++;
}

// ---- Map ----

ObjMap* newMap(VM* vm) {
    ObjMap* map = ALLOCATE_OBJ(vm, ObjMap, OBJ_MAP);
    initTable(&map->table);
    return map;
}

// ---- Print ----

void printObject(Value value) {
    switch (OBJ_TYPE(value)) {
        case OBJ_STRING:
            printf("%s", AS_CSTRING(value));
            break;
        case OBJ_FUNCTION: {
            ObjFunction* fn = AS_FUNCTION(value);
            if (fn->name == NULL) {
                printf("<script>");
            } else {
                printf("<fn %s>", fn->name->chars);
            }
            break;
        }
        case OBJ_CLOSURE: {
            ObjFunction* fn = AS_CLOSURE(value)->function;
            if (fn->name == NULL) {
                printf("<script>");
            } else {
                printf("<fn %s>", fn->name->chars);
            }
            break;
        }
        case OBJ_UPVALUE:
            printf("<upvalue>");
            break;
        case OBJ_NATIVE: {
            ObjNative* native = AS_NATIVE(value);
            printf("<native %s>", native->name);
            break;
        }
        case OBJ_LIST: {
            ObjList* list = AS_LIST(value);
            printf("[");
            for (int i = 0; i < list->count; i++) {
                if (i > 0) printf(", ");
                printValue(list->items[i]);
            }
            printf("]");
            break;
        }
        case OBJ_MAP: {
            printf("{...}");
            break;
        }
    }
}

// ---- Free ----

void freeObject(Obj* object) {
    switch (object->type) {
        case OBJ_STRING: {
            ObjString* string = (ObjString*)object;
            reallocate(object, sizeof(ObjString) + string->length + 1, 0);
            break;
        }
        case OBJ_FUNCTION: {
            ObjFunction* function = (ObjFunction*)object;
            freeChunk(&function->chunk);
            FREE(ObjFunction, object);
            break;
        }
        case OBJ_CLOSURE: {
            ObjClosure* closure = (ObjClosure*)object;
            FREE_ARRAY(ObjUpvalue*, closure->upvalues, closure->upvalueCount);
            FREE(ObjClosure, object);
            break;
        }
        case OBJ_UPVALUE:
            FREE(ObjUpvalue, object);
            break;
        case OBJ_NATIVE:
            FREE(ObjNative, object);
            break;
        case OBJ_LIST: {
            ObjList* list = (ObjList*)object;
            FREE_ARRAY(Value, list->items, list->capacity);
            FREE(ObjList, object);
            break;
        }
        case OBJ_MAP: {
            ObjMap* map = (ObjMap*)object;
            freeTable(&map->table);
            FREE(ObjMap, object);
            break;
        }
    }
}
