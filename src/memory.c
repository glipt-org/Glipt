#include "memory.h"
#include "object.h"
#include "vm.h"

#ifdef DEBUG_STRESS_GC
#include <stdio.h>
#endif

// Global VM pointer for GC access during allocation
// (set by the VM when it starts running)
static VM* currentVM = NULL;

void setCurrentVM(VM* vm) {
    currentVM = vm;
}

void* reallocate(void* pointer, size_t oldSize, size_t newSize) {
    if (currentVM != NULL) {
        currentVM->bytesAllocated += newSize - oldSize;

#ifdef DEBUG_STRESS_GC
        if (newSize > oldSize) {
            collectGarbage(currentVM);
        }
#endif

        if (currentVM->bytesAllocated > currentVM->nextGC && newSize > oldSize) {
            collectGarbage(currentVM);
        }
    }

    if (newSize == 0) {
        free(pointer);
        return NULL;
    }

    void* result = realloc(pointer, newSize);
    if (result == NULL) {
        fprintf(stderr, "Error: Out of memory.\n");
        exit(1);
    }
    return result;
}

// ---- Mark Phase ----

static void markValue(Value value) {
    if (IS_OBJ(value)) markObject(AS_OBJ(value));
}

static void markArray(ValueArray* array) {
    for (int i = 0; i < array->count; i++) {
        markValue(array->values[i]);
    }
}

void markObject(Obj* object) {
    if (object == NULL) return;
    if (object->isMarked) return;

#ifdef DEBUG_TRACE
    printf("  mark %p ", (void*)object);
    printValue(OBJ_VAL(object));
    printf("\n");
#endif

    object->isMarked = true;

    if (currentVM->grayCapacity < currentVM->grayCount + 1) {
        currentVM->grayCapacity = GROW_CAPACITY(currentVM->grayCapacity);
        currentVM->grayStack = (Obj**)realloc(currentVM->grayStack,
            sizeof(Obj*) * currentVM->grayCapacity);
        if (currentVM->grayStack == NULL) {
            fprintf(stderr, "Error: Out of memory (gray stack).\n");
            exit(1);
        }
    }

    currentVM->grayStack[currentVM->grayCount++] = object;
}

static void markRoots(VM* vm) {
    // Mark the stack
    for (Value* slot = vm->stack; slot < vm->stackTop; slot++) {
        markValue(*slot);
    }

    // Mark call frame closures
    for (int i = 0; i < vm->frameCount; i++) {
        markObject((Obj*)vm->frames[i].closure);
    }

    // Mark open upvalues
    for (ObjUpvalue* upvalue = vm->openUpvalues; upvalue != NULL;
         upvalue = upvalue->next) {
        markObject((Obj*)upvalue);
    }

    // Mark globals
    markTable(&vm->globals);

    // Mark module cache (import system)
    markTable(&vm->modules);

    // Mark compiler roots (if compiling)
    // (This will be called by the compiler when needed)
}

// ---- Trace Phase ----

static void blackenObject(Obj* object) {
#ifdef DEBUG_TRACE
    printf("  blacken %p ", (void*)object);
    printValue(OBJ_VAL(object));
    printf("\n");
#endif

    switch (object->type) {
        case OBJ_CLOSURE: {
            ObjClosure* closure = (ObjClosure*)object;
            markObject((Obj*)closure->function);
            for (int i = 0; i < closure->upvalueCount; i++) {
                markObject((Obj*)closure->upvalues[i]);
            }
            break;
        }
        case OBJ_FUNCTION: {
            ObjFunction* function = (ObjFunction*)object;
            markObject((Obj*)function->name);
            markArray(&function->chunk.constants);
            break;
        }
        case OBJ_UPVALUE:
            markValue(((ObjUpvalue*)object)->closed);
            break;
        case OBJ_LIST: {
            ObjList* list = (ObjList*)object;
            for (int i = 0; i < list->count; i++) {
                markValue(list->items[i]);
            }
            break;
        }
        case OBJ_MAP: {
            ObjMap* map = (ObjMap*)object;
            markTable(&map->table);
            break;
        }
        case OBJ_NATIVE:
        case OBJ_STRING:
            break;
    }
}

static void traceReferences(VM* vm) {
    while (vm->grayCount > 0) {
        Obj* object = vm->grayStack[--vm->grayCount];
        blackenObject(object);
    }
}

// ---- Sweep Phase ----

static void sweep(VM* vm) {
    Obj* previous = NULL;
    Obj* object = vm->objects;
    while (object != NULL) {
        if (object->isMarked) {
            object->isMarked = false;
            previous = object;
            object = object->next;
        } else {
            Obj* unreached = object;
            object = object->next;
            if (previous != NULL) {
                previous->next = object;
            } else {
                vm->objects = object;
            }
            freeObject(unreached);
        }
    }
}

void collectGarbage(VM* vm) {
#ifdef DEBUG_TRACE
    printf("-- gc begin\n");
    size_t before = vm->bytesAllocated;
#endif

    markRoots(vm);
    traceReferences(vm);
    tableRemoveWhite(&vm->strings);
    sweep(vm);

    vm->nextGC = vm->bytesAllocated * 2;

#ifdef DEBUG_TRACE
    printf("-- gc end\n");
    printf("   collected %zu bytes (from %zu to %zu) next at %zu\n",
           before - vm->bytesAllocated, before, vm->bytesAllocated, vm->nextGC);
#endif
}

void freeObjects(VM* vm) {
    Obj* object = vm->objects;
    while (object != NULL) {
        Obj* next = object->next;
        freeObject(object);
        object = next;
    }
    free(vm->grayStack);
}
