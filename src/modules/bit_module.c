#if !defined(_WIN32) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE
#endif

#include "bit_module.h"
#include "../object.h"
#include "../table.h"

#include <stdint.h>

static Value bitAndNative(VM* vm, int argCount, Value* args) {
    (void)vm; (void)argCount;
    if (!IS_NUMBER(args[0]) || !IS_NUMBER(args[1])) return NIL_VAL;
    uint32_t a = (uint32_t)AS_NUMBER(args[0]);
    uint32_t b = (uint32_t)AS_NUMBER(args[1]);
    return NUMBER_VAL((double)(a & b));
}

static Value bitOrNative(VM* vm, int argCount, Value* args) {
    (void)vm; (void)argCount;
    if (!IS_NUMBER(args[0]) || !IS_NUMBER(args[1])) return NIL_VAL;
    uint32_t a = (uint32_t)AS_NUMBER(args[0]);
    uint32_t b = (uint32_t)AS_NUMBER(args[1]);
    return NUMBER_VAL((double)(a | b));
}

static Value bitXorNative(VM* vm, int argCount, Value* args) {
    (void)vm; (void)argCount;
    if (!IS_NUMBER(args[0]) || !IS_NUMBER(args[1])) return NIL_VAL;
    uint32_t a = (uint32_t)AS_NUMBER(args[0]);
    uint32_t b = (uint32_t)AS_NUMBER(args[1]);
    return NUMBER_VAL((double)(a ^ b));
}

static Value bitNotNative(VM* vm, int argCount, Value* args) {
    (void)vm; (void)argCount;
    if (!IS_NUMBER(args[0])) return NIL_VAL;
    uint32_t a = (uint32_t)AS_NUMBER(args[0]);
    return NUMBER_VAL((double)(~a));
}

static Value bitLshiftNative(VM* vm, int argCount, Value* args) {
    (void)vm; (void)argCount;
    if (!IS_NUMBER(args[0]) || !IS_NUMBER(args[1])) return NIL_VAL;
    uint32_t a = (uint32_t)AS_NUMBER(args[0]);
    int n = (int)AS_NUMBER(args[1]);
    if (n < 0 || n >= 32) return NUMBER_VAL(0);
    return NUMBER_VAL((double)(a << n));
}

static Value bitRshiftNative(VM* vm, int argCount, Value* args) {
    (void)vm; (void)argCount;
    if (!IS_NUMBER(args[0]) || !IS_NUMBER(args[1])) return NIL_VAL;
    uint32_t a = (uint32_t)AS_NUMBER(args[0]);
    int n = (int)AS_NUMBER(args[1]);
    if (n < 0 || n >= 32) return NUMBER_VAL(0);
    return NUMBER_VAL((double)(a >> n));
}

void registerBitModule(VM* vm) {
    ObjMap* bitMod = newMap(vm);
    vmPush(vm, OBJ_VAL(bitMod));

    defineModuleNative(vm, bitMod, "and", bitAndNative, 2);
    defineModuleNative(vm, bitMod, "or", bitOrNative, 2);
    defineModuleNative(vm, bitMod, "xor", bitXorNative, 2);
    defineModuleNative(vm, bitMod, "not", bitNotNative, 1);
    defineModuleNative(vm, bitMod, "lshift", bitLshiftNative, 2);
    defineModuleNative(vm, bitMod, "rshift", bitRshiftNative, 2);

    ObjString* modName = copyString(vm, "bit", 3);
    tableSet(&vm->globals, modName, OBJ_VAL(bitMod));
    vmPop(vm);
}
