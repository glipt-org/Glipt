#if !defined(_WIN32) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE
#endif

#include "math_module.h"
#include "../object.h"
#include "../table.h"

#ifdef _WIN32

void registerMathModule(VM* vm) {
    ObjMap* mathMod = newMap(vm);
    vmPush(vm, OBJ_VAL(mathMod));
    ObjString* name = copyString(vm, "math", 4);
    tableSet(&vm->globals, name, OBJ_VAL(mathMod));
    vmPop(vm);
}

#else

#include <math.h>
#include <stdlib.h>
#include <time.h>

// ---- Rounding & Basic ----

static Value mathFloorNative(VM* vm, int argCount, Value* args) {
    (void)vm; (void)argCount;
    if (!IS_NUMBER(args[0])) return NIL_VAL;
    return NUMBER_VAL(floor(AS_NUMBER(args[0])));
}

static Value mathCeilNative(VM* vm, int argCount, Value* args) {
    (void)vm; (void)argCount;
    if (!IS_NUMBER(args[0])) return NIL_VAL;
    return NUMBER_VAL(ceil(AS_NUMBER(args[0])));
}

static Value mathRoundNative(VM* vm, int argCount, Value* args) {
    (void)vm; (void)argCount;
    if (!IS_NUMBER(args[0])) return NIL_VAL;
    return NUMBER_VAL(round(AS_NUMBER(args[0])));
}

static Value mathAbsNative(VM* vm, int argCount, Value* args) {
    (void)vm; (void)argCount;
    if (!IS_NUMBER(args[0])) return NIL_VAL;
    return NUMBER_VAL(fabs(AS_NUMBER(args[0])));
}

static Value mathSqrtNative(VM* vm, int argCount, Value* args) {
    (void)vm; (void)argCount;
    if (!IS_NUMBER(args[0])) return NIL_VAL;
    return NUMBER_VAL(sqrt(AS_NUMBER(args[0])));
}

static Value mathPowNative(VM* vm, int argCount, Value* args) {
    (void)vm; (void)argCount;
    if (!IS_NUMBER(args[0]) || !IS_NUMBER(args[1])) return NIL_VAL;
    return NUMBER_VAL(pow(AS_NUMBER(args[0]), AS_NUMBER(args[1])));
}

static Value mathLogNative(VM* vm, int argCount, Value* args) {
    (void)vm; (void)argCount;
    if (!IS_NUMBER(args[0])) return NIL_VAL;
    return NUMBER_VAL(log(AS_NUMBER(args[0])));
}

static Value mathLog10Native(VM* vm, int argCount, Value* args) {
    (void)vm; (void)argCount;
    if (!IS_NUMBER(args[0])) return NIL_VAL;
    return NUMBER_VAL(log10(AS_NUMBER(args[0])));
}

static Value mathExpNative(VM* vm, int argCount, Value* args) {
    (void)vm; (void)argCount;
    if (!IS_NUMBER(args[0])) return NIL_VAL;
    return NUMBER_VAL(exp(AS_NUMBER(args[0])));
}

// ---- Min / Max ----

static Value mathMinNative(VM* vm, int argCount, Value* args) {
    (void)vm; (void)argCount;
    if (!IS_NUMBER(args[0]) || !IS_NUMBER(args[1])) return NIL_VAL;
    double a = AS_NUMBER(args[0]);
    double b = AS_NUMBER(args[1]);
    return NUMBER_VAL(a < b ? a : b);
}

static Value mathMaxNative(VM* vm, int argCount, Value* args) {
    (void)vm; (void)argCount;
    if (!IS_NUMBER(args[0]) || !IS_NUMBER(args[1])) return NIL_VAL;
    double a = AS_NUMBER(args[0]);
    double b = AS_NUMBER(args[1]);
    return NUMBER_VAL(a > b ? a : b);
}

// ---- Trigonometry ----

static Value mathSinNative(VM* vm, int argCount, Value* args) {
    (void)vm; (void)argCount;
    if (!IS_NUMBER(args[0])) return NIL_VAL;
    return NUMBER_VAL(sin(AS_NUMBER(args[0])));
}

static Value mathCosNative(VM* vm, int argCount, Value* args) {
    (void)vm; (void)argCount;
    if (!IS_NUMBER(args[0])) return NIL_VAL;
    return NUMBER_VAL(cos(AS_NUMBER(args[0])));
}

static Value mathTanNative(VM* vm, int argCount, Value* args) {
    (void)vm; (void)argCount;
    if (!IS_NUMBER(args[0])) return NIL_VAL;
    return NUMBER_VAL(tan(AS_NUMBER(args[0])));
}

static Value mathAsinNative(VM* vm, int argCount, Value* args) {
    (void)vm; (void)argCount;
    if (!IS_NUMBER(args[0])) return NIL_VAL;
    return NUMBER_VAL(asin(AS_NUMBER(args[0])));
}

static Value mathAcosNative(VM* vm, int argCount, Value* args) {
    (void)vm; (void)argCount;
    if (!IS_NUMBER(args[0])) return NIL_VAL;
    return NUMBER_VAL(acos(AS_NUMBER(args[0])));
}

static Value mathAtanNative(VM* vm, int argCount, Value* args) {
    (void)vm; (void)argCount;
    if (!IS_NUMBER(args[0])) return NIL_VAL;
    return NUMBER_VAL(atan(AS_NUMBER(args[0])));
}

static Value mathAtan2Native(VM* vm, int argCount, Value* args) {
    (void)vm; (void)argCount;
    if (!IS_NUMBER(args[0]) || !IS_NUMBER(args[1])) return NIL_VAL;
    return NUMBER_VAL(atan2(AS_NUMBER(args[0]), AS_NUMBER(args[1])));
}

// ---- Random ----

static Value mathRandNative(VM* vm, int argCount, Value* args) {
    (void)vm; (void)argCount; (void)args;
    return NUMBER_VAL((double)rand() / (double)RAND_MAX);
}

static Value mathRandIntNative(VM* vm, int argCount, Value* args) {
    (void)vm; (void)argCount;
    if (!IS_NUMBER(args[0]) || !IS_NUMBER(args[1])) return NIL_VAL;
    int min = (int)AS_NUMBER(args[0]);
    int max = (int)AS_NUMBER(args[1]);
    if (max < min) return NIL_VAL;
    return NUMBER_VAL((double)(min + rand() % (max - min + 1)));
}

// ---- Module Registration ----

void registerMathModule(VM* vm) {
    srand((unsigned int)time(NULL));

    ObjMap* mathMod = newMap(vm);
    vmPush(vm, OBJ_VAL(mathMod));

    // Rounding & Basic
    defineModuleNative(vm, mathMod, "floor", mathFloorNative, 1);
    defineModuleNative(vm, mathMod, "ceil", mathCeilNative, 1);
    defineModuleNative(vm, mathMod, "round", mathRoundNative, 1);
    defineModuleNative(vm, mathMod, "abs", mathAbsNative, 1);
    defineModuleNative(vm, mathMod, "sqrt", mathSqrtNative, 1);
    defineModuleNative(vm, mathMod, "pow", mathPowNative, 2);
    defineModuleNative(vm, mathMod, "log", mathLogNative, 1);
    defineModuleNative(vm, mathMod, "log10", mathLog10Native, 1);
    defineModuleNative(vm, mathMod, "exp", mathExpNative, 1);

    // Min / Max
    defineModuleNative(vm, mathMod, "min", mathMinNative, 2);
    defineModuleNative(vm, mathMod, "max", mathMaxNative, 2);

    // Trigonometry
    defineModuleNative(vm, mathMod, "sin", mathSinNative, 1);
    defineModuleNative(vm, mathMod, "cos", mathCosNative, 1);
    defineModuleNative(vm, mathMod, "tan", mathTanNative, 1);
    defineModuleNative(vm, mathMod, "asin", mathAsinNative, 1);
    defineModuleNative(vm, mathMod, "acos", mathAcosNative, 1);
    defineModuleNative(vm, mathMod, "atan", mathAtanNative, 1);
    defineModuleNative(vm, mathMod, "atan2", mathAtan2Native, 2);

    // Random
    defineModuleNative(vm, mathMod, "rand", mathRandNative, 0);
    defineModuleNative(vm, mathMod, "rand_int", mathRandIntNative, 2);

    // Constants
    ObjString* piKey = copyString(vm, "PI", 2);
    tableSet(&mathMod->table, piKey, NUMBER_VAL(3.14159265358979323846));

    ObjString* eKey = copyString(vm, "E", 1);
    tableSet(&mathMod->table, eKey, NUMBER_VAL(2.71828182845904523536));

    ObjString* infKey = copyString(vm, "INF", 3);
    tableSet(&mathMod->table, infKey, NUMBER_VAL(INFINITY));

    ObjString* nanKey = copyString(vm, "NAN", 3);
    tableSet(&mathMod->table, nanKey, NUMBER_VAL(NAN));

    ObjString* modName = copyString(vm, "math", 4);
    tableSet(&vm->globals, modName, OBJ_VAL(mathMod));
    vmPop(vm);
}

#endif
