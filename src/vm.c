#if !defined(_WIN32) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE
#endif

#include "vm.h"
#include "compiler.h"
#include "debug.h"
#include "memory.h"
#include "object.h"

#include "process.h"
#include "dataformat.h"
#include "parallel.h"

#include "modules/fs.h"
#include "modules/proc.h"
#include "modules/net.h"
#include "modules/sys.h"
#include "modules/math_module.h"
#include "modules/regex.h"

#include <stdarg.h>
#include <math.h>
#include <time.h>
#include <ctype.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

// Forward declarations for calling closures from native functions
static bool callClosure(VM* vm, ObjClosure* closure, int argCount);
static InterpretResult run(VM* vm);

// Forward declarations
static inline void push(VM* vm, Value value);
static inline Value pop(VM* vm);
static void runtimeError(VM* vm, const char* format, ...);
static void raiseError(VM* vm, const char* message, const char* type);

// ---- Native Functions ----

static Value clockNative(VM* vm, int argCount, Value* args) {
    (void)vm; (void)argCount; (void)args;
    return NUMBER_VAL((double)clock() / CLOCKS_PER_SEC);
}

static Value lenNative(VM* vm, int argCount, Value* args) {
    (void)vm;
    if (argCount != 1) return NIL_VAL;

    if (IS_STRING(args[0])) {
        return NUMBER_VAL(AS_STRING(args[0])->length);
    }
    if (IS_LIST(args[0])) {
        return NUMBER_VAL(AS_LIST(args[0])->count);
    }
    return NIL_VAL;
}

static Value typeNative(VM* vm, int argCount, Value* args) {
    if (argCount != 1) return OBJ_VAL(copyString(vm, "nil", 3));

    Value val = args[0];
    const char* name;
    if (IS_BOOL(val))        name = "bool";
    else if (IS_NIL(val))    name = "nil";
    else if (IS_NUMBER(val)) name = "number";
    else if (IS_OBJ(val)) {
        switch (OBJ_TYPE(val)) {
            case OBJ_STRING:   name = "string"; break;
            case OBJ_FUNCTION:
            case OBJ_CLOSURE:
            case OBJ_NATIVE:   name = "function"; break;
            case OBJ_LIST:     name = "list"; break;
            case OBJ_MAP:      name = "map"; break;
            default:           name = "object"; break;
        }
    } else {
        name = "unknown";
    }
    return OBJ_VAL(copyString(vm, name, (int)strlen(name)));
}

static Value strNative(VM* vm, int argCount, Value* args) {
    if (argCount != 1) return OBJ_VAL(copyString(vm, "", 0));

    Value val = args[0];
    if (IS_STRING(val)) return val;

    char buf[64];
    int len;
    if (IS_NUMBER(val)) {
        double num = AS_NUMBER(val);
        if (num == (int)num) {
            len = snprintf(buf, sizeof(buf), "%d", (int)num);
        } else {
            len = snprintf(buf, sizeof(buf), "%g", num);
        }
    } else if (IS_BOOL(val)) {
        len = snprintf(buf, sizeof(buf), "%s", AS_BOOL(val) ? "true" : "false");
    } else if (IS_NIL(val)) {
        len = snprintf(buf, sizeof(buf), "nil");
    } else {
        len = snprintf(buf, sizeof(buf), "<object>");
    }
    return OBJ_VAL(copyString(vm, buf, len));
}

static Value appendNative(VM* vm, int argCount, Value* args) {
    if (argCount != 2 || !IS_LIST(args[0])) return NIL_VAL;
    listAppend(vm, AS_LIST(args[0]), args[1]);
    return args[0];
}

static Value popNative(VM* vm, int argCount, Value* args) {
    (void)vm;
    if (argCount != 1 || !IS_LIST(args[0])) return NIL_VAL;
    ObjList* list = AS_LIST(args[0]);
    if (list->count == 0) return NIL_VAL;
    list->count--;
    return list->items[list->count];
}

static Value printNative(VM* vm, int argCount, Value* args) {
    (void)vm;
    for (int i = 0; i < argCount; i++) {
        if (i > 0) printf(" ");
        printValue(args[i]);
    }
    printf("\n");
    return NIL_VAL;
}

static Value printlnNative(VM* vm, int argCount, Value* args) {
    return printNative(vm, argCount, args);
}

static Value inputNative(VM* vm, int argCount, Value* args) {
    if (argCount > 0 && IS_STRING(args[0])) {
        printf("%s", AS_CSTRING(args[0]));
        fflush(stdout);
    }
    char buffer[1024];
    if (fgets(buffer, sizeof(buffer), stdin) != NULL) {
        int len = (int)strlen(buffer);
        if (len > 0 && buffer[len - 1] == '\n') len--;
        return OBJ_VAL(copyString(vm, buffer, len));
    }
    return NIL_VAL;
}

static Value exitNative(VM* vm, int argCount, Value* args) {
    (void)vm;
    if (argCount > 0) {
        if (IS_NUMBER(args[0])) {
            exit((int)AS_NUMBER(args[0]));
        } else if (IS_STRING(args[0])) {
            fprintf(stderr, "%s\n", AS_CSTRING(args[0]));
            exit(1);
        }
    }
    exit(0);
}

static Value keysNative(VM* vm, int argCount, Value* args) {
    if (argCount != 1 || !IS_MAP(args[0])) return NIL_VAL;
    ObjMap* map = AS_MAP(args[0]);
    ObjList* list = newList(vm);

    for (int i = 0; i < map->table.capacity; i++) {
        Entry* entry = &map->table.entries[i];
        if (entry->key != NULL) {
            listAppend(vm, list, OBJ_VAL(entry->key));
        }
    }
    return OBJ_VAL(list);
}

static Value valuesNative(VM* vm, int argCount, Value* args) {
    if (argCount != 1 || !IS_MAP(args[0])) return NIL_VAL;
    ObjMap* map = AS_MAP(args[0]);
    ObjList* list = newList(vm);

    for (int i = 0; i < map->table.capacity; i++) {
        Entry* entry = &map->table.entries[i];
        if (entry->key != NULL) {
            listAppend(vm, list, entry->value);
        }
    }
    return OBJ_VAL(list);
}

static Value containsNative(VM* vm, int argCount, Value* args) {
    (void)vm;
    if (argCount != 2) return BOOL_VAL(false);

    if (IS_LIST(args[0])) {
        ObjList* list = AS_LIST(args[0]);
        for (int i = 0; i < list->count; i++) {
            if (valuesEqual(list->items[i], args[1])) return BOOL_VAL(true);
        }
        return BOOL_VAL(false);
    }
    if (IS_STRING(args[0]) && IS_STRING(args[1])) {
        return BOOL_VAL(strstr(AS_CSTRING(args[0]), AS_CSTRING(args[1])) != NULL);
    }
    if (IS_MAP(args[0]) && IS_STRING(args[1])) {
        Value dummy;
        return BOOL_VAL(tableGet(&AS_MAP(args[0])->table, AS_STRING(args[1]), &dummy));
    }
    return BOOL_VAL(false);
}

static Value rangeNative(VM* vm, int argCount, Value* args) {
    if (argCount < 2 || !IS_NUMBER(args[0]) || !IS_NUMBER(args[1])) return NIL_VAL;
    double start = AS_NUMBER(args[0]);
    double end = AS_NUMBER(args[1]);
    double step = 1;
    if (argCount >= 3 && IS_NUMBER(args[2])) step = AS_NUMBER(args[2]);
    if (step == 0) return NIL_VAL;

    ObjList* list = newList(vm);
    if (step > 0) {
        for (double i = start; i < end; i += step) {
            listAppend(vm, list, NUMBER_VAL(i));
        }
    } else {
        for (double i = start; i > end; i += step) {
            listAppend(vm, list, NUMBER_VAL(i));
        }
    }
    return OBJ_VAL(list);
}

static Value joinNative(VM* vm, int argCount, Value* args) {
    if (argCount < 1 || !IS_LIST(args[0])) return OBJ_VAL(copyString(vm, "", 0));
    ObjList* list = AS_LIST(args[0]);
    const char* sep = "";
    int sepLen = 0;
    if (argCount >= 2 && IS_STRING(args[1])) {
        sep = AS_CSTRING(args[1]);
        sepLen = AS_STRING(args[1])->length;
    }

    // Calculate total length
    int totalLen = 0;
    for (int i = 0; i < list->count; i++) {
        if (IS_STRING(list->items[i])) {
            totalLen += AS_STRING(list->items[i])->length;
        } else {
            totalLen += 20; // estimate for non-strings
        }
        if (i > 0) totalLen += sepLen;
    }

    char* buffer = ALLOCATE(char, totalLen + 1);
    int pos = 0;
    for (int i = 0; i < list->count; i++) {
        if (i > 0) {
            memcpy(buffer + pos, sep, sepLen);
            pos += sepLen;
        }
        if (IS_STRING(list->items[i])) {
            ObjString* s = AS_STRING(list->items[i]);
            memcpy(buffer + pos, s->chars, s->length);
            pos += s->length;
        } else {
            // Convert to string representation
            char tmp[64];
            int len;
            if (IS_NUMBER(list->items[i])) {
                double num = AS_NUMBER(list->items[i]);
                if (num == (int)num) {
                    len = snprintf(tmp, sizeof(tmp), "%d", (int)num);
                } else {
                    len = snprintf(tmp, sizeof(tmp), "%g", num);
                }
            } else if (IS_BOOL(list->items[i])) {
                len = snprintf(tmp, sizeof(tmp), "%s", AS_BOOL(list->items[i]) ? "true" : "false");
            } else {
                len = snprintf(tmp, sizeof(tmp), "nil");
            }
            memcpy(buffer + pos, tmp, len);
            pos += len;
        }
    }
    buffer[pos] = '\0';

    ObjString* result = copyString(vm, buffer, pos);
    FREE_ARRAY(char, buffer, totalLen + 1);
    return OBJ_VAL(result);
}

static Value execNative(VM* vm, int argCount, Value* args) {
    if (argCount < 1 || !IS_STRING(args[0])) {
        return NIL_VAL;
    }

    const char* command = AS_CSTRING(args[0]);

    // Permission check
    if (!hasPermission(&vm->permissions, PERM_EXEC, command)) {
        char msg[512];
        snprintf(msg, sizeof(msg), "Permission denied: exec \"%s\"", command);
        raiseError(vm, msg, "permission");
        return NIL_VAL;
    }

    ProcessResult result = processExec(command);

    // Build result map: {stdout: "...", stderr: "...", exitCode: N}
    ObjMap* map = newMap(vm);

    // Protect map from GC
    Value mapVal = OBJ_VAL(map);
    *vm->stackTop++ = mapVal;

    ObjString* stdoutKey = copyString(vm, "stdout", 6);
    ObjString* stdoutVal = copyString(vm, result.stdoutData ? result.stdoutData : "",
                                       result.stdoutLength);
    tableSet(&map->table, stdoutKey, OBJ_VAL(stdoutVal));

    ObjString* stderrKey = copyString(vm, "stderr", 6);
    ObjString* stderrVal = copyString(vm, result.stderrData ? result.stderrData : "",
                                       result.stderrLength);
    tableSet(&map->table, stderrKey, OBJ_VAL(stderrVal));

    ObjString* exitCodeKey = copyString(vm, "exitCode", 8);
    tableSet(&map->table, exitCodeKey, NUMBER_VAL(result.exitCode));

    // Strip trailing newline from stdout for convenience
    ObjString* outputKey = copyString(vm, "output", 6);
    int outLen = result.stdoutLength;
    if (outLen > 0 && result.stdoutData[outLen - 1] == '\n') outLen--;
    ObjString* outputVal = copyString(vm, result.stdoutData ? result.stdoutData : "", outLen);
    tableSet(&map->table, outputKey, OBJ_VAL(outputVal));

    vm->stackTop--; // unprotect map

    // Raise error on non-zero exit code
    if (result.exitCode != 0) {
        char msg[512];
        snprintf(msg, sizeof(msg), "Command failed with exit code %d: %s",
                 result.exitCode, command);
        raiseError(vm, msg, "exec");
    }

    processResultFree(&result);

    return mapVal;
}

static Value parseJsonNative(VM* vm, int argCount, Value* args) {
    if (argCount != 1 || !IS_STRING(args[0])) return NIL_VAL;
    ObjString* str = AS_STRING(args[0]);
    return parseJSON(vm, str->chars, str->length);
}

static Value toJsonNative(VM* vm, int argCount, Value* args) {
    if (argCount != 1) return OBJ_VAL(copyString(vm, "null", 4));
    return toJSON(vm, args[0]);
}

static Value readFileNative(VM* vm, int argCount, Value* args) {
    if (argCount != 1 || !IS_STRING(args[0])) return NIL_VAL;

    const char* path = AS_CSTRING(args[0]);

    // Permission check
    if (!hasPermission(&vm->permissions, PERM_READ, path)) {
        char msg[512];
        snprintf(msg, sizeof(msg), "Permission denied: read \"%s\"", path);
        raiseError(vm, msg, "permission");
        return NIL_VAL;
    }

    FILE* file = fopen(path, "rb");
    if (file == NULL) return NIL_VAL;

    fseek(file, 0L, SEEK_END);
    size_t size = ftell(file);
    rewind(file);

    char* buffer = (char*)malloc(size + 1);
    if (buffer == NULL) {
        fclose(file);
        return NIL_VAL;
    }

    size_t bytesRead = fread(buffer, 1, size, file);
    buffer[bytesRead] = '\0';
    fclose(file);

    // Auto-detect format by extension
    int pathLen = AS_STRING(args[0])->length;
    if (pathLen > 5 && memcmp(path + pathLen - 5, ".json", 5) == 0) {
        Value result = parseJSON(vm, buffer, (int)bytesRead);
        free(buffer);
        return result;
    }

    ObjString* result = copyString(vm, buffer, (int)bytesRead);
    free(buffer);
    return OBJ_VAL(result);
}

static Value writeFileNative(VM* vm, int argCount, Value* args) {
    if (argCount != 2 || !IS_STRING(args[0]) || !IS_STRING(args[1])) {
        return BOOL_VAL(false);
    }

    const char* path = AS_CSTRING(args[0]);

    // Permission check
    if (!hasPermission(&vm->permissions, PERM_WRITE, path)) {
        char msg[512];
        snprintf(msg, sizeof(msg), "Permission denied: write \"%s\"", path);
        raiseError(vm, msg, "permission");
        return BOOL_VAL(false);
    }
    ObjString* content = AS_STRING(args[1]);

    FILE* file = fopen(path, "wb");
    if (file == NULL) return BOOL_VAL(false);

    size_t written = fwrite(content->chars, 1, content->length, file);
    fclose(file);

    return BOOL_VAL(written == (size_t)content->length);
}

static Value envNative(VM* vm, int argCount, Value* args) {
    if (argCount != 1 || !IS_STRING(args[0])) return NIL_VAL;

    const char* name = AS_CSTRING(args[0]);
    if (!hasPermission(&vm->permissions, PERM_ENV, name)) {
        char msg[512];
        snprintf(msg, sizeof(msg), "Permission denied: env \"%s\"", name);
        raiseError(vm, msg, "permission");
        return NIL_VAL;
    }

    const char* val = getenv(name);
    if (val == NULL) return NIL_VAL;
    return OBJ_VAL(copyString(vm, val, (int)strlen(val)));
}

static Value sleepNative(VM* vm, int argCount, Value* args) {
    (void)vm;
    if (argCount != 1 || !IS_NUMBER(args[0])) return NIL_VAL;
    double seconds = AS_NUMBER(args[0]);
    if (seconds > 0) {
#ifdef _WIN32
        Sleep((DWORD)(seconds * 1000));
#else
        struct timespec ts;
        ts.tv_sec = (time_t)seconds;
        ts.tv_nsec = (long)((seconds - (time_t)seconds) * 1e9);
        nanosleep(&ts, NULL);
#endif
    }
    return NIL_VAL;
}

static Value assertNative(VM* vm, int argCount, Value* args) {
    (void)vm;
    if (argCount < 1) return NIL_VAL;
    if (isFalsey(args[0])) {
        if (argCount >= 2 && IS_STRING(args[1])) {
            fprintf(stderr, "Assertion failed: %s\n", AS_CSTRING(args[1]));
        } else {
            fprintf(stderr, "Assertion failed\n");
        }
        exit(1);
    }
    return BOOL_VAL(true);
}

static Value splitNative(VM* vm, int argCount, Value* args) {
    if (argCount != 2 || !IS_STRING(args[0]) || !IS_STRING(args[1])) return NIL_VAL;
    ObjString* str = AS_STRING(args[0]);
    ObjString* delim = AS_STRING(args[1]);
    ObjList* list = newList(vm);

    // Protect from GC
    *vm->stackTop++ = OBJ_VAL(list);

    if (delim->length == 0) {
        // Split into characters
        for (int i = 0; i < str->length; i++) {
            listAppend(vm, list, OBJ_VAL(copyString(vm, &str->chars[i], 1)));
        }
    } else {
        const char* start = str->chars;
        const char* end = str->chars + str->length;
        const char* pos;

        while ((pos = strstr(start, delim->chars)) != NULL) {
            listAppend(vm, list, OBJ_VAL(copyString(vm, start, (int)(pos - start))));
            start = pos + delim->length;
        }
        listAppend(vm, list, OBJ_VAL(copyString(vm, start, (int)(end - start))));
    }

    vm->stackTop--;
    return OBJ_VAL(list);
}

static Value trimNative(VM* vm, int argCount, Value* args) {
    if (argCount != 1 || !IS_STRING(args[0])) return NIL_VAL;
    ObjString* str = AS_STRING(args[0]);
    int start = 0, end = str->length;
    while (start < end && (str->chars[start] == ' ' || str->chars[start] == '\t' ||
           str->chars[start] == '\n' || str->chars[start] == '\r')) start++;
    while (end > start && (str->chars[end-1] == ' ' || str->chars[end-1] == '\t' ||
           str->chars[end-1] == '\n' || str->chars[end-1] == '\r')) end--;
    return OBJ_VAL(copyString(vm, str->chars + start, end - start));
}

static Value replaceNative(VM* vm, int argCount, Value* args) {
    if (argCount != 3 || !IS_STRING(args[0]) || !IS_STRING(args[1]) || !IS_STRING(args[2]))
        return NIL_VAL;
    ObjString* str = AS_STRING(args[0]);
    ObjString* old = AS_STRING(args[1]);
    ObjString* newStr = AS_STRING(args[2]);

    if (old->length == 0) return args[0];

    // Count occurrences
    int count = 0;
    const char* pos = str->chars;
    while ((pos = strstr(pos, old->chars)) != NULL) {
        count++;
        pos += old->length;
    }
    if (count == 0) return args[0];

    int newLen = str->length + count * (newStr->length - old->length);
    char* buf = (char*)malloc(newLen + 1);
    if (buf == NULL) return NIL_VAL;
    char* out = buf;
    const char* src = str->chars;

    while ((pos = strstr(src, old->chars)) != NULL) {
        int chunk = (int)(pos - src);
        memcpy(out, src, chunk);
        out += chunk;
        memcpy(out, newStr->chars, newStr->length);
        out += newStr->length;
        src = pos + old->length;
    }
    int remaining = (int)(str->chars + str->length - src);
    memcpy(out, src, remaining);
    out[remaining] = '\0';

    ObjString* result = copyString(vm, buf, newLen);
    free(buf);
    return OBJ_VAL(result);
}

static Value upperNative(VM* vm, int argCount, Value* args) {
    if (argCount != 1 || !IS_STRING(args[0])) return NIL_VAL;
    ObjString* str = AS_STRING(args[0]);
    char* buf = (char*)malloc(str->length + 1);
    if (buf == NULL) return NIL_VAL;
    for (int i = 0; i < str->length; i++) {
        buf[i] = (char)toupper((unsigned char)str->chars[i]);
    }
    buf[str->length] = '\0';
    ObjString* result = copyString(vm, buf, str->length);
    free(buf);
    return OBJ_VAL(result);
}

static Value lowerNative(VM* vm, int argCount, Value* args) {
    if (argCount != 1 || !IS_STRING(args[0])) return NIL_VAL;
    ObjString* str = AS_STRING(args[0]);
    char* buf = (char*)malloc(str->length + 1);
    if (buf == NULL) return NIL_VAL;
    for (int i = 0; i < str->length; i++) {
        buf[i] = (char)tolower((unsigned char)str->chars[i]);
    }
    buf[str->length] = '\0';
    ObjString* result = copyString(vm, buf, str->length);
    free(buf);
    return OBJ_VAL(result);
}

static Value startsWithNative(VM* vm, int argCount, Value* args) {
    (void)vm;
    if (argCount != 2 || !IS_STRING(args[0]) || !IS_STRING(args[1])) return BOOL_VAL(false);
    ObjString* str = AS_STRING(args[0]);
    ObjString* prefix = AS_STRING(args[1]);
    if (prefix->length > str->length) return BOOL_VAL(false);
    return BOOL_VAL(memcmp(str->chars, prefix->chars, prefix->length) == 0);
}

static Value endsWithNative(VM* vm, int argCount, Value* args) {
    (void)vm;
    if (argCount != 2 || !IS_STRING(args[0]) || !IS_STRING(args[1])) return BOOL_VAL(false);
    ObjString* str = AS_STRING(args[0]);
    ObjString* suffix = AS_STRING(args[1]);
    if (suffix->length > str->length) return BOOL_VAL(false);
    return BOOL_VAL(memcmp(str->chars + str->length - suffix->length,
                            suffix->chars, suffix->length) == 0);
}

static Value numNative(VM* vm, int argCount, Value* args) {
    (void)vm;
    if (argCount != 1) return NIL_VAL;
    if (IS_NUMBER(args[0])) return args[0];
    if (IS_STRING(args[0])) {
        char* end;
        double result = strtod(AS_CSTRING(args[0]), &end);
        if (end == AS_CSTRING(args[0])) return NIL_VAL;
        return NUMBER_VAL(result);
    }
    if (IS_BOOL(args[0])) return NUMBER_VAL(AS_BOOL(args[0]) ? 1 : 0);
    return NIL_VAL;
}

static Value sortNative(VM* vm, int argCount, Value* args) {
    (void)vm;
    if (argCount != 1 || !IS_LIST(args[0])) return NIL_VAL;
    ObjList* list = AS_LIST(args[0]);

    // Simple bubble sort (good enough for scripting)
    for (int i = 0; i < list->count - 1; i++) {
        for (int j = 0; j < list->count - i - 1; j++) {
            if (IS_NUMBER(list->items[j]) && IS_NUMBER(list->items[j+1])) {
                if (AS_NUMBER(list->items[j]) > AS_NUMBER(list->items[j+1])) {
                    Value tmp = list->items[j];
                    list->items[j] = list->items[j+1];
                    list->items[j+1] = tmp;
                }
            }
        }
    }
    return args[0];
}

// Call a closure/native from native context, returning the result.
// The caller must have already pushed the callee and argCount arguments
// onto vm->stackTop before calling this.
static Value vmCallFunction(VM* vm, Value callee, int argCount) {
    if (IS_NATIVE(callee)) {
        Value result = AS_NATIVE(callee)->function(vm, argCount,
            vm->stackTop - argCount);
        vm->stackTop -= argCount + 1;
        return result;
    }

    if (IS_CLOSURE(callee)) {
        ObjClosure* closure = AS_CLOSURE(callee);
        if (!callClosure(vm, closure, argCount)) {
            vm->stackTop -= argCount + 1;
            return NIL_VAL;
        }
        int savedBase = vm->baseFrameCount;
        vm->baseFrameCount = vm->frameCount - 1;
        InterpretResult result = run(vm);
        vm->baseFrameCount = savedBase;
        if (result != INTERPRET_OK) {
            return NIL_VAL;
        }
        // run() left the result on the stack via OP_RETURN
        return pop(vm);
    }

    vm->stackTop -= argCount + 1;
    return NIL_VAL;
}

static Value mapFnNative(VM* vm, int argCount, Value* args) {
    if (argCount != 2 || !IS_LIST(args[0])) return NIL_VAL;
    ObjList* list = AS_LIST(args[0]);
    Value fn = args[1];
    ObjList* result = newList(vm);

    *vm->stackTop++ = OBJ_VAL(result);

    for (int i = 0; i < list->count; i++) {
        *vm->stackTop++ = fn;
        *vm->stackTop++ = list->items[i];
        Value res = vmCallFunction(vm, fn, 1);
        if (vm->hasError) {
            vm->stackTop--; // pop result protection
            return NIL_VAL;
        }
        listAppend(vm, result, res);
    }

    vm->stackTop--; // pop result protection
    return OBJ_VAL(result);
}

static Value filterNative(VM* vm, int argCount, Value* args) {
    if (argCount != 2 || !IS_LIST(args[0])) return NIL_VAL;
    ObjList* list = AS_LIST(args[0]);
    Value fn = args[1];
    ObjList* result = newList(vm);

    *vm->stackTop++ = OBJ_VAL(result);

    for (int i = 0; i < list->count; i++) {
        *vm->stackTop++ = fn;
        *vm->stackTop++ = list->items[i];
        Value res = vmCallFunction(vm, fn, 1);
        if (vm->hasError) {
            vm->stackTop--; // pop result protection
            return NIL_VAL;
        }
        if (!isFalsey(res)) {
            listAppend(vm, result, list->items[i]);
        }
    }

    vm->stackTop--;
    return OBJ_VAL(result);
}

static Value boolNative(VM* vm, int argCount, Value* args) {
    (void)vm;
    if (argCount != 1) return BOOL_VAL(false);
    return BOOL_VAL(!isFalsey(args[0]));
}

static Value reduceNative(VM* vm, int argCount, Value* args) {
    if (argCount < 2 || !IS_LIST(args[0])) return NIL_VAL;
    ObjList* list = AS_LIST(args[0]);
    Value fn = args[1];
    if (list->count == 0) return argCount >= 3 ? args[2] : NIL_VAL;

    Value acc = argCount >= 3 ? args[2] : list->items[0];
    int start = argCount >= 3 ? 0 : 1;

    for (int i = start; i < list->count; i++) {
        *vm->stackTop++ = fn;
        *vm->stackTop++ = acc;
        *vm->stackTop++ = list->items[i];
        acc = vmCallFunction(vm, fn, 2);
        if (vm->hasError) return NIL_VAL;
    }
    return acc;
}

static Value debugNative(VM* vm, int argCount, Value* args) {
    (void)vm;
    for (int i = 0; i < argCount; i++) {
        if (i > 0) fprintf(stderr, " ");
        fprintf(stderr, "[DEBUG] ");
        printValue(args[i]);
    }
    fprintf(stderr, "\n");
    return NIL_VAL;
}

static Value formatNative(VM* vm, int argCount, Value* args) {
    if (argCount < 1 || !IS_STRING(args[0])) return NIL_VAL;
    ObjString* fmt = AS_STRING(args[0]);

    // Simple {} placeholder replacement
    int bufSize = fmt->length * 2 + 256;
    char* buf = (char*)malloc(bufSize);
    if (buf == NULL) return NIL_VAL;
    int out = 0;
    int argIdx = 1;

    for (int i = 0; i < fmt->length; i++) {
        if (i + 1 < fmt->length && fmt->chars[i] == '{' && fmt->chars[i + 1] == '}') {
            if (argIdx < argCount) {
                // Print value to a temp buffer
                char temp[256];
                int len = 0;
                Value v = args[argIdx++];
                if (IS_NUMBER(v)) {
                    double n = AS_NUMBER(v);
                    if (n == (int)n) len = snprintf(temp, sizeof(temp), "%d", (int)n);
                    else len = snprintf(temp, sizeof(temp), "%g", n);
                } else if (IS_BOOL(v)) {
                    len = snprintf(temp, sizeof(temp), "%s", AS_BOOL(v) ? "true" : "false");
                } else if (IS_NIL(v)) {
                    len = snprintf(temp, sizeof(temp), "nil");
                } else if (IS_STRING(v)) {
                    ObjString* s = AS_STRING(v);
                    len = s->length < (int)sizeof(temp) - 1 ? s->length : (int)sizeof(temp) - 1;
                    memcpy(temp, s->chars, len);
                } else {
                    len = snprintf(temp, sizeof(temp), "<object>");
                }
                if (out + len >= bufSize) {
                    bufSize = (out + len) * 2;
                    char* newBuf = (char*)realloc(buf, bufSize);
                    if (newBuf == NULL) { free(buf); return NIL_VAL; }
                    buf = newBuf;
                }
                memcpy(buf + out, temp, len);
                out += len;
            }
            i++; // skip '}'
        } else {
            if (out + 1 >= bufSize) {
                bufSize *= 2;
                char* newBuf = (char*)realloc(buf, bufSize);
                if (newBuf == NULL) { free(buf); return NIL_VAL; }
                buf = newBuf;
            }
            buf[out++] = fmt->chars[i];
        }
    }
    buf[out] = '\0';

    ObjString* result = copyString(vm, buf, out);
    free(buf);
    return OBJ_VAL(result);
}

// ---- VM Init ----

void initVM(VM* vm) {
    vm->stackTop = vm->stack;
    vm->frameCount = 0;

    initTable(&vm->globals);
    initTable(&vm->strings);

    vm->openUpvalues = NULL;
    vm->objects = NULL;
    vm->grayCount = 0;
    vm->grayCapacity = 0;
    vm->grayStack = NULL;
    vm->bytesAllocated = 0;
    vm->nextGC = 1024 * 1024; // 1 MB initial threshold

    initPermissions(&vm->permissions);
    vm->handlerCount = 0;
    vm->hasError = false;
    vm->currentError = NIL_VAL;
    memset(vm->globalIC, 0, sizeof(vm->globalIC));

    vm->baseFrameCount = 0;
    vm->scriptArgc = 0;
    vm->scriptArgv = NULL;

    initTable(&vm->modules);
    vm->scriptPath = NULL;

    setCurrentVM(vm);

    // Built-in functions
    defineNative(vm, "print", printNative, -1);
    defineNative(vm, "println", printlnNative, -1);
    defineNative(vm, "input", inputNative, -1);
    defineNative(vm, "exit", exitNative, -1);
    defineNative(vm, "clock", clockNative, 0);
    defineNative(vm, "len", lenNative, 1);
    defineNative(vm, "type", typeNative, 1);
    defineNative(vm, "str", strNative, 1);
    defineNative(vm, "append", appendNative, 2);
    defineNative(vm, "pop", popNative, 1);
    defineNative(vm, "keys", keysNative, 1);
    defineNative(vm, "values", valuesNative, 1);
    defineNative(vm, "contains", containsNative, 2);
    defineNative(vm, "range", rangeNative, -1);
    defineNative(vm, "join", joinNative, -1);
    defineNative(vm, "exec", execNative, -1);

    defineNative(vm, "parse_json", parseJsonNative, 1);
    defineNative(vm, "to_json", toJsonNative, 1);

    defineNative(vm, "read", readFileNative, 1);
    defineNative(vm, "write", writeFileNative, 2);

    defineNative(vm, "env", envNative, 1);
    defineNative(vm, "sleep", sleepNative, 1);
    defineNative(vm, "assert", assertNative, -1);

    // Strings
    defineNative(vm, "split", splitNative, 2);
    defineNative(vm, "trim", trimNative, 1);
    defineNative(vm, "replace", replaceNative, 3);
    defineNative(vm, "upper", upperNative, 1);
    defineNative(vm, "lower", lowerNative, 1);
    defineNative(vm, "starts_with", startsWithNative, 2);
    defineNative(vm, "ends_with", endsWithNative, 2);

    // Collections
    defineNative(vm, "sort", sortNative, 1);
    defineNative(vm, "map_fn", mapFnNative, 2);
    defineNative(vm, "filter", filterNative, 2);
    defineNative(vm, "reduce", reduceNative, -1);

    // Type conversions
    defineNative(vm, "num", numNative, 1);
    defineNative(vm, "bool", boolNative, 1);

    defineNative(vm, "format", formatNative, -1);
    defineNative(vm, "debug", debugNative, -1);
    defineNative(vm, "parallel_exec", parallelExec, 1);

    // Standard library modules (registered as global maps: fs, net, proc, sys, math, re)
    registerFsModule(vm);
    registerProcModule(vm);
    registerNetModule(vm);
    registerSysModule(vm);
    registerMathModule(vm);
    registerRegexModule(vm);
}

void freeVM(VM* vm) {
    freeTable(&vm->globals);
    freeTable(&vm->strings);
    freeTable(&vm->modules);
    freePermissions(&vm->permissions);
    freeObjects(vm);
    setCurrentVM(NULL);
}

void defineNative(VM* vm, const char* name, NativeFn function, int arity) {
    ObjString* nameStr = copyString(vm, name, (int)strlen(name));
    // Push both objects to protect from GC during tableSet
    *vm->stackTop++ = OBJ_VAL(nameStr);
    *vm->stackTop++ = OBJ_VAL(newNative(vm, function, name, arity));
    tableSet(&vm->globals, AS_STRING(vm->stack[0]), vm->stack[1]);
    vm->stackTop -= 2;
}

void defineModuleNative(VM* vm, ObjMap* module, const char* name,
                        NativeFn function, int arity) {
    ObjString* nameStr = copyString(vm, name, (int)strlen(name));
    *vm->stackTop++ = OBJ_VAL(nameStr);
    *vm->stackTop++ = OBJ_VAL(newNative(vm, function, name, arity));
    tableSet(&module->table, AS_STRING(vm->stackTop[-2]), vm->stackTop[-1]);
    vm->stackTop -= 2;
}

void vmPush(VM* vm, Value value) {
    if (vm->stackTop >= vm->stack + STACK_MAX) {
        fprintf(stderr, "Stack overflow.\n");
        return;
    }
    *vm->stackTop = value;
    vm->stackTop++;
}

Value vmPop(VM* vm) {
    vm->stackTop--;
    return *vm->stackTop;
}

void vmRaiseError(VM* vm, const char* message, const char* type) {
    ObjMap* errorMap = newMap(vm);
    vmPush(vm, OBJ_VAL(errorMap));

    ObjString* msgKey = copyString(vm, "message", 7);
    ObjString* msgVal = copyString(vm, message, (int)strlen(message));
    tableSet(&errorMap->table, msgKey, OBJ_VAL(msgVal));

    ObjString* typeKey = copyString(vm, "type", 4);
    ObjString* typeVal = copyString(vm, type, (int)strlen(type));
    tableSet(&errorMap->table, typeKey, OBJ_VAL(typeVal));

    vmPop(vm);

    vm->hasError = true;
    vm->currentError = OBJ_VAL(errorMap);
}

// ---- Runtime Errors ----

static void runtimeError(VM* vm, const char* format, ...) {
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fputs("\n", stderr);

    for (int i = vm->frameCount - 1; i >= 0; i--) {
        CallFrame* frame = &vm->frames[i];
        ObjFunction* function = frame->closure->function;
        size_t instruction = frame->ip - function->chunk.code - 1;
        fprintf(stderr, "[line %d] in ", function->chunk.lines[instruction]);
        if (function->name == NULL) {
            fprintf(stderr, "script\n");
        } else {
            fprintf(stderr, "%s()\n", function->name->chars);
        }
    }

    vm->stackTop = vm->stack;
    vm->frameCount = 0;
}

// Build the error map and mark the VM as having a pending error.
// The dispatch loop checks vm->hasError after each operation and jumps to the
// nearest handler, or prints and terminates if no handler is registered.
static void raiseError(VM* vm, const char* message, const char* type) {
    ObjMap* errorMap = newMap(vm);
    push(vm, OBJ_VAL(errorMap)); // GC protection while building the map

    ObjString* msgKey = copyString(vm, "message", 7);
    ObjString* msgVal = copyString(vm, message, (int)strlen(message));
    tableSet(&errorMap->table, msgKey, OBJ_VAL(msgVal));

    ObjString* typeKey = copyString(vm, "type", 4);
    ObjString* typeVal = copyString(vm, type, (int)strlen(type));
    tableSet(&errorMap->table, typeKey, OBJ_VAL(typeVal));

    pop(vm);

    vm->hasError = true;
    vm->currentError = OBJ_VAL(errorMap);
}

// ---- Stack Operations ----

static inline void push(VM* vm, Value value) {
    if (vm->stackTop >= vm->stack + STACK_MAX) {
        fprintf(stderr, "Stack overflow.\n");
        vm->stackTop = vm->stack;
        vm->frameCount = 0;
        return;
    }
    *vm->stackTop = value;
    vm->stackTop++;
}

static inline Value pop(VM* vm) {
    vm->stackTop--;
    return *vm->stackTop;
}

static inline Value peek(VM* vm, int distance) {
    return vm->stackTop[-1 - distance];
}

// ---- Call ----

static bool callClosure(VM* vm, ObjClosure* closure, int argCount) {
    if (argCount != closure->function->arity) {
        runtimeError(vm, "Expected %d arguments but got %d.",
                     closure->function->arity, argCount);
        return false;
    }

    if (vm->frameCount == FRAMES_MAX) {
        runtimeError(vm, "Stack overflow.");
        return false;
    }

    CallFrame* frame = &vm->frames[vm->frameCount++];
    frame->closure = closure;
    frame->ip = closure->function->chunk.code;
    frame->slots = vm->stackTop - argCount - 1;
    return true;
}

static bool callValue(VM* vm, Value callee, int argCount) {
    if (IS_OBJ(callee)) {
        switch (OBJ_TYPE(callee)) {
            case OBJ_CLOSURE:
                return callClosure(vm, AS_CLOSURE(callee), argCount);
            case OBJ_NATIVE: {
                ObjNative* native = AS_NATIVE(callee);
                if (native->arity >= 0 && argCount != native->arity) {
                    runtimeError(vm, "Expected %d arguments but got %d.",
                                 native->arity, argCount);
                    return false;
                }
                Value result = native->function(vm, argCount,
                    vm->stackTop - argCount);
                vm->stackTop -= argCount + 1;
                push(vm, result);
                return true;
            }
            default:
                break;
        }
    }
    runtimeError(vm, "Can only call functions.");
    return false;
}

// ---- Upvalue Operations ----

static ObjUpvalue* captureUpvalue(VM* vm, Value* local) {
    ObjUpvalue* prevUpvalue = NULL;
    ObjUpvalue* upvalue = vm->openUpvalues;

    while (upvalue != NULL && upvalue->location > local) {
        prevUpvalue = upvalue;
        upvalue = upvalue->next;
    }

    if (upvalue != NULL && upvalue->location == local) {
        return upvalue;
    }

    ObjUpvalue* createdUpvalue = newUpvalue(vm, local);
    createdUpvalue->next = upvalue;

    if (prevUpvalue == NULL) {
        vm->openUpvalues = createdUpvalue;
    } else {
        prevUpvalue->next = createdUpvalue;
    }

    return createdUpvalue;
}

static void closeUpvalues(VM* vm, Value* last) {
    while (vm->openUpvalues != NULL && vm->openUpvalues->location >= last) {
        ObjUpvalue* upvalue = vm->openUpvalues;
        upvalue->closed = *upvalue->location;
        upvalue->location = &upvalue->closed;
        vm->openUpvalues = upvalue->next;
    }
}

// ---- String Concatenation ----

static void concatenate(VM* vm) {
    ObjString* b = AS_STRING(peek(vm, 0));
    ObjString* a = AS_STRING(peek(vm, 1));

    int length = a->length + b->length;
    char* chars = ALLOCATE(char, length + 1);
    memcpy(chars, a->chars, a->length);
    memcpy(chars + a->length, b->chars, b->length);
    chars[length] = '\0';

    ObjString* result = takeString(vm, chars, length);
    pop(vm);
    pop(vm);
    push(vm, OBJ_VAL(result));
}

// ---- Execution Loop ----

static InterpretResult run(VM* vm) {
    CallFrame* frame = &vm->frames[vm->frameCount - 1];

    // 'ip' and 'constants' are cached in locals to avoid pointer chasing on every instruction
    register uint8_t* ip = frame->ip;
    register Value* constants = frame->closure->function->chunk.constants.values;

    // STORE_FRAME before any call that may need the current ip (errors, callValue)
    // LOAD_FRAME after any call that may switch to a new frame (calls, returns)
#define STORE_FRAME() (frame->ip = ip)
#define LOAD_FRAME() \
    do { \
        frame = &vm->frames[vm->frameCount - 1]; \
        ip = frame->ip; \
        constants = frame->closure->function->chunk.constants.values; \
    } while (false)

#define READ_BYTE() (*ip++)
#define READ_SHORT() \
    (ip += 2, (uint16_t)((ip[-2] << 8) | ip[-1]))
#define READ_CONSTANT() (constants[READ_BYTE()])
#define READ_STRING() AS_STRING(READ_CONSTANT())
#define BINARY_OP(valueType, op) \
    do { \
        if (!IS_NUMBER(vm->stackTop[-1]) || !IS_NUMBER(vm->stackTop[-2])) { \
            STORE_FRAME(); \
            runtimeError(vm, "Operands must be numbers."); \
            return INTERPRET_RUNTIME_ERROR; \
        } \
        double b = AS_NUMBER(vm->stackTop[-1]); \
        double a = AS_NUMBER(vm->stackTop[-2]); \
        vm->stackTop--; \
        vm->stackTop[-1] = valueType(a op b); \
    } while (false)

    // Computed goto dispatch avoids indirect branch misprediction from switch (~15-25% faster on GCC/Clang)
#if defined(__GNUC__) || defined(__clang__)
#define USE_COMPUTED_GOTO
#endif

#ifdef USE_COMPUTED_GOTO
    static void* dispatch_table[] = {
        [OP_CONSTANT]      = &&op_CONSTANT,
        [OP_NIL]           = &&op_NIL,
        [OP_TRUE]          = &&op_TRUE,
        [OP_FALSE]         = &&op_FALSE,
        [OP_ADD]           = &&op_ADD,
        [OP_SUBTRACT]      = &&op_SUBTRACT,
        [OP_MULTIPLY]      = &&op_MULTIPLY,
        [OP_DIVIDE]        = &&op_DIVIDE,
        [OP_MODULO]        = &&op_MODULO,
        [OP_NEGATE]        = &&op_NEGATE,
        [OP_EQUAL]         = &&op_EQUAL,
        [OP_NOT_EQUAL]     = &&op_NOT_EQUAL,
        [OP_GREATER]       = &&op_GREATER,
        [OP_GREATER_EQUAL] = &&op_GREATER_EQUAL,
        [OP_LESS]          = &&op_LESS,
        [OP_LESS_EQUAL]    = &&op_LESS_EQUAL,
        [OP_NOT]           = &&op_NOT,
        [OP_GET_LOCAL]     = &&op_GET_LOCAL,
        [OP_SET_LOCAL]     = &&op_SET_LOCAL,
        [OP_GET_GLOBAL]    = &&op_GET_GLOBAL,
        [OP_SET_GLOBAL]    = &&op_SET_GLOBAL,
        [OP_DEFINE_GLOBAL] = &&op_DEFINE_GLOBAL,
        [OP_GET_UPVALUE]   = &&op_GET_UPVALUE,
        [OP_SET_UPVALUE]   = &&op_SET_UPVALUE,
        [OP_JUMP]          = &&op_JUMP,
        [OP_JUMP_IF_FALSE] = &&op_JUMP_IF_FALSE,
        [OP_LOOP]          = &&op_LOOP,
        [OP_CALL]          = &&op_CALL,
        [OP_CLOSURE]       = &&op_CLOSURE,
        [OP_RETURN]        = &&op_RETURN,
        [OP_CLOSE_UPVALUE] = &&op_CLOSE_UPVALUE,
        [OP_BUILD_LIST]    = &&op_BUILD_LIST,
        [OP_BUILD_MAP]     = &&op_BUILD_MAP,
        [OP_INDEX_GET]     = &&op_INDEX_GET,
        [OP_INDEX_SET]     = &&op_INDEX_SET,
        [OP_GET_PROPERTY]  = &&op_GET_PROPERTY,
        [OP_SET_PROPERTY]  = &&op_SET_PROPERTY,
        [OP_PRINT]         = &&op_PRINT,
        [OP_POP]           = &&op_POP,
        [OP_ALLOW]         = &&op_ALLOW,
        [OP_PUSH_HANDLER]  = &&op_PUSH_HANDLER,
        [OP_POP_HANDLER]   = &&op_POP_HANDLER,
        [OP_THROW]         = &&op_THROW,
        [OP_IMPORT]        = &&op_IMPORT,
    };

    #define DISPATCH() goto *dispatch_table[READ_BYTE()]
    #define CASE(name) op_##name
    #define NEXT() DISPATCH()
    #define LOOP_START DISPATCH();
    #define LOOP_END
#else
    #define DISPATCH()
    #define CASE(name) case OP_##name
    #define NEXT() break
    #define LOOP_START for (;;) { uint8_t instruction; switch (instruction = READ_BYTE()) {
    #define LOOP_END }}
#endif

    LOOP_START

#ifdef DEBUG_TRACE
        // Print stack
        STORE_FRAME();
        printf("          ");
        for (Value* slot = vm->stack; slot < vm->stackTop; slot++) {
            printf("[ ");
            printValue(*slot);
            printf(" ]");
        }
        printf("\n");
        disassembleInstruction(&frame->closure->function->chunk,
            (int)(ip - frame->closure->function->chunk.code));
#endif

    CASE(CONSTANT): {
        *vm->stackTop++ = READ_CONSTANT();
        NEXT();
    }

    CASE(NIL):   *vm->stackTop++ = NIL_VAL; NEXT();
    CASE(TRUE):  *vm->stackTop++ = TRUE_VAL; NEXT();
    CASE(FALSE): *vm->stackTop++ = FALSE_VAL; NEXT();

    CASE(ADD): {
        if (IS_NUMBER(vm->stackTop[-1]) && IS_NUMBER(vm->stackTop[-2])) {
            double b = AS_NUMBER(vm->stackTop[-1]);
            double a = AS_NUMBER(vm->stackTop[-2]);
            vm->stackTop--;
            vm->stackTop[-1] = NUMBER_VAL(a + b);
        } else if (IS_STRING(vm->stackTop[-1]) && IS_STRING(vm->stackTop[-2])) {
            concatenate(vm);
        } else {
            STORE_FRAME();
            runtimeError(vm, "Operands must be two numbers or two strings.");
            return INTERPRET_RUNTIME_ERROR;
        }
        NEXT();
    }

    CASE(SUBTRACT): BINARY_OP(NUMBER_VAL, -); NEXT();
    CASE(MULTIPLY): BINARY_OP(NUMBER_VAL, *); NEXT();
    CASE(DIVIDE): {
        if (!IS_NUMBER(vm->stackTop[-1]) || !IS_NUMBER(vm->stackTop[-2])) {
            STORE_FRAME();
            runtimeError(vm, "Operands must be numbers.");
            return INTERPRET_RUNTIME_ERROR;
        }
        double b = AS_NUMBER(vm->stackTop[-1]);
        if (b == 0) {
            STORE_FRAME();
            runtimeError(vm, "Division by zero.");
            return INTERPRET_RUNTIME_ERROR;
        }
        double a = AS_NUMBER(vm->stackTop[-2]);
        vm->stackTop--;
        vm->stackTop[-1] = NUMBER_VAL(a / b);
        NEXT();
    }
    CASE(MODULO): {
        if (!IS_NUMBER(vm->stackTop[-1]) || !IS_NUMBER(vm->stackTop[-2])) {
            STORE_FRAME();
            runtimeError(vm, "Operands must be numbers.");
            return INTERPRET_RUNTIME_ERROR;
        }
        double b = AS_NUMBER(vm->stackTop[-1]);
        double a = AS_NUMBER(vm->stackTop[-2]);
        vm->stackTop--;
        vm->stackTop[-1] = NUMBER_VAL(fmod(a, b));
        NEXT();
    }

    CASE(NEGATE): {
        if (!IS_NUMBER(vm->stackTop[-1])) {
            STORE_FRAME();
            runtimeError(vm, "Operand must be a number.");
            return INTERPRET_RUNTIME_ERROR;
        }
        vm->stackTop[-1] = NUMBER_VAL(-AS_NUMBER(vm->stackTop[-1]));
        NEXT();
    }

    CASE(EQUAL): {
        Value b = vm->stackTop[-1];
        Value a = vm->stackTop[-2];
        vm->stackTop--;
        vm->stackTop[-1] = BOOL_VAL(valuesEqual(a, b));
        NEXT();
    }
    CASE(NOT_EQUAL): {
        Value b = vm->stackTop[-1];
        Value a = vm->stackTop[-2];
        vm->stackTop--;
        vm->stackTop[-1] = BOOL_VAL(!valuesEqual(a, b));
        NEXT();
    }
    CASE(GREATER):       BINARY_OP(BOOL_VAL, >); NEXT();
    CASE(GREATER_EQUAL): BINARY_OP(BOOL_VAL, >=); NEXT();
    CASE(LESS):          BINARY_OP(BOOL_VAL, <); NEXT();
    CASE(LESS_EQUAL):    BINARY_OP(BOOL_VAL, <=); NEXT();

    CASE(NOT):
        vm->stackTop[-1] = BOOL_VAL(isFalsey(vm->stackTop[-1]));
        NEXT();

    CASE(GET_LOCAL): {
        uint8_t slot = READ_BYTE();
        *vm->stackTop++ = frame->slots[slot];
        NEXT();
    }
    CASE(SET_LOCAL): {
        uint8_t slot = READ_BYTE();
        frame->slots[slot] = vm->stackTop[-1];
        NEXT();
    }

    CASE(GET_GLOBAL): {
        ObjString* name = READ_STRING();
        uint32_t slot = name->hash & (GLOBAL_IC_SIZE - 1);
        GlobalICSlot* ic = &vm->globalIC[slot];
        if (LIKELY(ic->key == name && ic->tableCapacity == vm->globals.capacity)) {
            *vm->stackTop++ = ic->entry->value;
        } else {
            Entry* entry;
            if (UNLIKELY(!tableGetEntry(&vm->globals, name, &entry))) {
                STORE_FRAME();
                runtimeError(vm, "Undefined variable '%s'.", name->chars);
                return INTERPRET_RUNTIME_ERROR;
            }
            ic->key = name;
            ic->entry = entry;
            ic->tableCapacity = vm->globals.capacity;
            *vm->stackTop++ = entry->value;
        }
        NEXT();
    }
    CASE(SET_GLOBAL): {
        ObjString* name = READ_STRING();
        Value value = vm->stackTop[-1];
        uint32_t slot = name->hash & (GLOBAL_IC_SIZE - 1);
        GlobalICSlot* ic = &vm->globalIC[slot];
        if (LIKELY(ic->key == name && ic->tableCapacity == vm->globals.capacity)) {
            ic->entry->value = value;
        } else {
            tableSet(&vm->globals, name, value);
            Entry* entry;
            tableGetEntry(&vm->globals, name, &entry);
            ic->key = name;
            ic->entry = entry;
            ic->tableCapacity = vm->globals.capacity;
        }
        NEXT();
    }
    CASE(DEFINE_GLOBAL): {
        ObjString* name = READ_STRING();
        tableSet(&vm->globals, name, vm->stackTop[-1]);
        vm->stackTop--;
        NEXT();
    }

    CASE(GET_UPVALUE): {
        uint8_t slot = READ_BYTE();
        *vm->stackTop++ = *frame->closure->upvalues[slot]->location;
        NEXT();
    }
    CASE(SET_UPVALUE): {
        uint8_t slot = READ_BYTE();
        *frame->closure->upvalues[slot]->location = vm->stackTop[-1];
        NEXT();
    }

    CASE(JUMP): {
        uint16_t offset = READ_SHORT();
        ip += offset;
        NEXT();
    }
    CASE(JUMP_IF_FALSE): {
        uint16_t offset = READ_SHORT();
        if (isFalsey(vm->stackTop[-1])) ip += offset;
        NEXT();
    }
    CASE(LOOP): {
        uint16_t offset = READ_SHORT();
        ip -= offset;
        NEXT();
    }

    CASE(CALL): {
        int argCount = READ_BYTE();
        STORE_FRAME();
        if (!callValue(vm, peek(vm, argCount), argCount)) {
            return INTERPRET_RUNTIME_ERROR;
        }
        LOAD_FRAME();

        // Check for raised errors (e.g. from exec, permission denied)
        if (vm->hasError) {
            if (vm->handlerCount > 0) {
                ErrorHandler* handler = &vm->handlers[vm->handlerCount - 1];
                // Unwind to handler state
                vm->frameCount = handler->frameCount;
                LOAD_FRAME();
                vm->stackTop = handler->stackTop;
                // Push error value for the handler to use
                push(vm, vm->currentError);
                ip = handler->handlerIP;
                vm->hasError = false;
                vm->currentError = NIL_VAL;
            } else {
                // No handler - print error and terminate
                if (IS_OBJ(vm->currentError) && IS_MAP(vm->currentError)) {
                    ObjMap* errMap = AS_MAP(vm->currentError);
                    Value msgVal;
                    ObjString* msgKey = copyString(vm, "message", 7);
                    if (tableGet(&errMap->table, msgKey, &msgVal) && IS_STRING(msgVal)) {
                        STORE_FRAME();
                        runtimeError(vm, "%s", AS_CSTRING(msgVal));
                    } else {
                        STORE_FRAME();
                        runtimeError(vm, "Runtime error.");
                    }
                } else {
                    STORE_FRAME();
                    runtimeError(vm, "Runtime error.");
                }
                vm->hasError = false;
                vm->currentError = NIL_VAL;
                return INTERPRET_RUNTIME_ERROR;
            }
        }
        NEXT();
    }

    CASE(CLOSURE): {
        ObjFunction* function = AS_FUNCTION(READ_CONSTANT());
        ObjClosure* closure = newClosure(vm, function);
        push(vm, OBJ_VAL(closure));
        for (int i = 0; i < closure->upvalueCount; i++) {
            uint8_t isLocal = READ_BYTE();
            uint8_t index = READ_BYTE();
            if (isLocal) {
                closure->upvalues[i] = captureUpvalue(vm, frame->slots + index);
            } else {
                closure->upvalues[i] = frame->closure->upvalues[index];
            }
        }
        NEXT();
    }

    CASE(CLOSE_UPVALUE):
        closeUpvalues(vm, vm->stackTop - 1);
        pop(vm);
        NEXT();

    CASE(RETURN): {
        Value result = pop(vm);
        closeUpvalues(vm, frame->slots);
        vm->frameCount--;
        if (vm->frameCount == 0) {
            pop(vm);
            return INTERPRET_OK;
        }
        if (vm->baseFrameCount > 0 && vm->frameCount == vm->baseFrameCount) {
            vm->stackTop = frame->slots;
            push(vm, result);
            return INTERPRET_OK;
        }
        vm->stackTop = frame->slots;
        push(vm, result);
        LOAD_FRAME();
        NEXT();
    }

    CASE(PRINT): {
        printValue(*(--vm->stackTop));
        printf("\n");
        NEXT();
    }

    CASE(POP):
        vm->stackTop--;
        NEXT();

    CASE(BUILD_LIST): {
        int count = READ_BYTE();
        ObjList* list = newList(vm);
        push(vm, OBJ_VAL(list));

        for (int i = count; i > 0; i--) {
            listAppend(vm, list, vm->stackTop[-1 - i]);
        }

        vm->stackTop -= count + 1;
        push(vm, OBJ_VAL(list));
        NEXT();
    }

    CASE(BUILD_MAP): {
        int count = READ_BYTE();
        ObjMap* map = newMap(vm);
        push(vm, OBJ_VAL(map));

        for (int i = count; i > 0; i--) {
            Value val = vm->stackTop[-1 - (2 * i - 1)];
            Value key = vm->stackTop[-1 - (2 * i)];
            if (!IS_STRING(key)) {
                STORE_FRAME();
                runtimeError(vm, "Map key must be a string.");
                return INTERPRET_RUNTIME_ERROR;
            }
            tableSet(&map->table, AS_STRING(key), val);
        }

        vm->stackTop -= 2 * count + 1;
        push(vm, OBJ_VAL(map));
        NEXT();
    }

    CASE(INDEX_GET): {
        Value index = pop(vm);
        Value obj = pop(vm);

        if (IS_LIST(obj)) {
            if (!IS_NUMBER(index)) {
                STORE_FRAME();
                runtimeError(vm, "List index must be a number.");
                return INTERPRET_RUNTIME_ERROR;
            }
            ObjList* list = AS_LIST(obj);
            int i = (int)AS_NUMBER(index);
            if (i < 0) i += list->count;
            if (i < 0 || i >= list->count) {
                STORE_FRAME();
                runtimeError(vm, "List index %d out of range (length %d).", i, list->count);
                return INTERPRET_RUNTIME_ERROR;
            }
            push(vm, list->items[i]);
        } else if (IS_MAP(obj)) {
            if (!IS_STRING(index)) {
                STORE_FRAME();
                runtimeError(vm, "Map key must be a string.");
                return INTERPRET_RUNTIME_ERROR;
            }
            ObjMap* map = AS_MAP(obj);
            Value value;
            if (tableGet(&map->table, AS_STRING(index), &value)) {
                push(vm, value);
            } else {
                push(vm, NIL_VAL);
            }
        } else if (IS_STRING(obj)) {
            if (!IS_NUMBER(index)) {
                STORE_FRAME();
                runtimeError(vm, "String index must be a number.");
                return INTERPRET_RUNTIME_ERROR;
            }
            ObjString* str = AS_STRING(obj);
            int i = (int)AS_NUMBER(index);
            if (i < 0) i += str->length;
            if (i < 0 || i >= str->length) {
                STORE_FRAME();
                runtimeError(vm, "String index out of range.");
                return INTERPRET_RUNTIME_ERROR;
            }
            push(vm, OBJ_VAL(copyString(vm, &str->chars[i], 1)));
        } else {
            STORE_FRAME();
            runtimeError(vm, "Only lists, maps, and strings support indexing.");
            return INTERPRET_RUNTIME_ERROR;
        }
        NEXT();
    }

    CASE(INDEX_SET): {
        Value value = pop(vm);
        Value index = pop(vm);
        Value obj = pop(vm);

        if (IS_LIST(obj)) {
            if (!IS_NUMBER(index)) {
                STORE_FRAME();
                runtimeError(vm, "List index must be a number.");
                return INTERPRET_RUNTIME_ERROR;
            }
            ObjList* list = AS_LIST(obj);
            int i = (int)AS_NUMBER(index);
            if (i < 0) i += list->count;
            if (i < 0 || i >= list->count) {
                STORE_FRAME();
                runtimeError(vm, "List index out of range.");
                return INTERPRET_RUNTIME_ERROR;
            }
            list->items[i] = value;
            push(vm, value);
        } else if (IS_MAP(obj)) {
            if (!IS_STRING(index)) {
                STORE_FRAME();
                runtimeError(vm, "Map key must be a string.");
                return INTERPRET_RUNTIME_ERROR;
            }
            tableSet(&AS_MAP(obj)->table, AS_STRING(index), value);
            push(vm, value);
        } else {
            STORE_FRAME();
            runtimeError(vm, "Only lists and maps support index assignment.");
            return INTERPRET_RUNTIME_ERROR;
        }
        NEXT();
    }

    CASE(GET_PROPERTY): {
        Value obj = peek(vm, 0);
        ObjString* name = READ_STRING();

        if (IS_MAP(obj)) {
            Value value;
            if (tableGet(&AS_MAP(obj)->table, name, &value)) {
                pop(vm);
                push(vm, value);
            } else {
                pop(vm);
                push(vm, NIL_VAL);
            }
        } else if (IS_LIST(obj)) {
            ObjList* list = AS_LIST(obj);
            if (name->length == 6 && memcmp(name->chars, "length", 6) == 0) {
                pop(vm);
                push(vm, NUMBER_VAL(list->count));
            } else {
                STORE_FRAME();
                runtimeError(vm, "List has no property '%s'.", name->chars);
                return INTERPRET_RUNTIME_ERROR;
            }
        } else if (IS_STRING(obj)) {
            ObjString* str = AS_STRING(obj);
            if (name->length == 6 && memcmp(name->chars, "length", 6) == 0) {
                pop(vm);
                push(vm, NUMBER_VAL(str->length));
            } else {
                STORE_FRAME();
                runtimeError(vm, "String has no property '%s'.", name->chars);
                return INTERPRET_RUNTIME_ERROR;
            }
        } else {
            STORE_FRAME();
            runtimeError(vm, "Only maps, lists, and strings have properties.");
            return INTERPRET_RUNTIME_ERROR;
        }
        NEXT();
    }

    CASE(SET_PROPERTY): {
        Value value = peek(vm, 0);
        Value obj = peek(vm, 1);
        ObjString* name = READ_STRING();

        if (!IS_MAP(obj)) {
            STORE_FRAME();
            runtimeError(vm, "Only maps support property assignment.");
            return INTERPRET_RUNTIME_ERROR;
        }

        tableSet(&AS_MAP(obj)->table, name, value);
        Value assignedValue = pop(vm);
        pop(vm);
        push(vm, assignedValue);
        NEXT();
    }

    CASE(ALLOW): {
        uint8_t permType = READ_BYTE();
        ObjString* target = READ_STRING();
        addPermission(&vm->permissions, (PermissionType)permType,
                      target->chars, target->length);
        NEXT();
    }

    CASE(PUSH_HANDLER): {
        uint16_t offset = READ_SHORT();
        if (vm->handlerCount >= HANDLER_MAX) {
            STORE_FRAME();
            runtimeError(vm, "Too many nested error handlers.");
            return INTERPRET_RUNTIME_ERROR;
        }
        ErrorHandler* handler = &vm->handlers[vm->handlerCount++];
        handler->handlerIP = ip + offset;
        handler->frameCount = vm->frameCount;
        handler->stackTop = vm->stackTop;
        NEXT();
    }

    CASE(POP_HANDLER):
        if (vm->handlerCount > 0) {
            vm->handlerCount--;
        }
        NEXT();

    CASE(THROW):
        NEXT();

    CASE(IMPORT): {
        ObjString* path = READ_STRING();
        ObjString* modName = READ_STRING();

        STORE_FRAME();

        // Check cache first
        Value cached;
        if (tableGet(&vm->modules, path, &cached)) {
            tableSet(&vm->globals, modName, cached);
            NEXT();
        }

        // Resolve file path relative to current script
        char fullPath[1024];
        if (vm->scriptPath != NULL) {
            const char* lastSlash = strrchr(vm->scriptPath, '/');
            int dirLen = lastSlash ? (int)(lastSlash - vm->scriptPath) : 0;
            if (dirLen > 0) {
                snprintf(fullPath, sizeof(fullPath), "%.*s/%s",
                         dirLen, vm->scriptPath, path->chars);
            } else {
                snprintf(fullPath, sizeof(fullPath), "%s", path->chars);
            }
        } else {
            snprintf(fullPath, sizeof(fullPath), "%s", path->chars);
        }

        // Auto-append .glipt extension if missing
        size_t fpLen = strlen(fullPath);
        if (fpLen < 6 || strcmp(fullPath + fpLen - 6, ".glipt") != 0) {
            if (fpLen + 7 < sizeof(fullPath)) {
                strcat(fullPath, ".glipt");
            }
        }

        // Read the module file
        FILE* modFile = fopen(fullPath, "rb");
        if (modFile == NULL) {
            runtimeError(vm, "Could not open module '%s' (resolved to '%s').",
                         path->chars, fullPath);
            return INTERPRET_RUNTIME_ERROR;
        }

        fseek(modFile, 0L, SEEK_END);
        size_t fileSize = (size_t)ftell(modFile);
        rewind(modFile);
        char* modSource = (char*)malloc(fileSize + 1);
        if (modSource == NULL) {
            fclose(modFile);
            runtimeError(vm, "Out of memory reading module '%s'.", path->chars);
            return INTERPRET_RUNTIME_ERROR;
        }
        size_t bytesRead = fread(modSource, 1, fileSize, modFile);
        modSource[bytesRead] = '\0';
        fclose(modFile);

        // Snapshot existing global keys so we can diff after module execution
        Table existingGlobals;
        initTable(&existingGlobals);
        tableAddAll(&vm->globals, &existingGlobals);

        // Compile the module source
        ObjFunction* modFunc = compile(vm, modSource);
        free(modSource);
        if (modFunc == NULL) {
            freeTable(&existingGlobals);
            runtimeError(vm, "Compilation error in module '%s'.", path->chars);
            return INTERPRET_RUNTIME_ERROR;
        }

        // Execute the module
        push(vm, OBJ_VAL(modFunc));
        ObjClosure* modClosure = newClosure(vm, modFunc);
        pop(vm);
        push(vm, OBJ_VAL(modClosure));

        if (!callClosure(vm, modClosure, 0)) {
            freeTable(&existingGlobals);
            runtimeError(vm, "Error calling module '%s'.", path->chars);
            return INTERPRET_RUNTIME_ERROR;
        }

        int savedBase = vm->baseFrameCount;
        vm->baseFrameCount = vm->frameCount - 1;
        InterpretResult modResult = run(vm);
        vm->baseFrameCount = savedBase;

        if (modResult != INTERPRET_OK) {
            freeTable(&existingGlobals);
            return INTERPRET_RUNTIME_ERROR;
        }

        // Diff globals: anything the module added goes into the module map,
        // then gets removed from globals to keep the namespace clean.
        ObjMap* moduleMap = newMap(vm);
        push(vm, OBJ_VAL(moduleMap));

        for (int i = 0; i < vm->globals.capacity; i++) {
            Entry* entry = &vm->globals.entries[i];
            if (entry->key == NULL) continue;

            Value dummy;
            if (!tableGet(&existingGlobals, entry->key, &dummy)) {
                tableSet(&moduleMap->table, entry->key, entry->value);
                tableDelete(&vm->globals, entry->key);
            }
        }

        pop(vm);
        freeTable(&existingGlobals);

        tableSet(&vm->modules, path, OBJ_VAL(moduleMap)); // cache for future imports
        tableSet(&vm->globals, modName, OBJ_VAL(moduleMap));

        LOAD_FRAME();
        NEXT();
    }

    LOOP_END

#undef STORE_FRAME
#undef LOAD_FRAME
#undef READ_BYTE
#undef READ_SHORT
#undef READ_CONSTANT
#undef READ_STRING
#undef BINARY_OP
#undef DISPATCH
#undef CASE
#undef NEXT
#undef LOOP_START
#undef LOOP_END
}

// ---- Public API ----

InterpretResult interpret(VM* vm, const char* source) {
    ObjFunction* function = compile(vm, source);
    if (function == NULL) return INTERPRET_COMPILE_ERROR;

    push(vm, OBJ_VAL(function));
    ObjClosure* closure = newClosure(vm, function);
    pop(vm);
    push(vm, OBJ_VAL(closure));
    callClosure(vm, closure, 0);

    return run(vm);
}
