#if !defined(_WIN32) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE
#endif

#include "regex.h"
#include "../object.h"
#include "../table.h"

#ifdef _WIN32

void registerRegexModule(VM* vm) {
    ObjMap* re = newMap(vm);
    vmPush(vm, OBJ_VAL(re));
    ObjString* name = copyString(vm, "re", 2);
    tableSet(&vm->globals, name, OBJ_VAL(re));
    vmPop(vm);
}

#else

#include <regex.h>
#include <string.h>
#include <stdlib.h>

static Value reMatchNative(VM* vm, int argCount, Value* args) {
    (void)argCount;
    if (!IS_STRING(args[0]) || !IS_STRING(args[1])) {
        vmRaiseError(vm, "re.match requires string arguments", "type");
        return BOOL_VAL(false);
    }

    regex_t reg;
    if (regcomp(&reg, AS_CSTRING(args[0]), REG_EXTENDED | REG_NOSUB) != 0) {
        vmRaiseError(vm, "Invalid regex pattern", "regex");
        return BOOL_VAL(false);
    }
    int result = regexec(&reg, AS_CSTRING(args[1]), 0, NULL, 0);
    regfree(&reg);
    return BOOL_VAL(result == 0);
}

static Value reSearchNative(VM* vm, int argCount, Value* args) {
    (void)argCount;
    if (!IS_STRING(args[0]) || !IS_STRING(args[1])) {
        vmRaiseError(vm, "re.search requires string arguments", "type");
        return NIL_VAL;
    }
    const char* str = AS_CSTRING(args[1]);

    regex_t reg;
    if (regcomp(&reg, AS_CSTRING(args[0]), REG_EXTENDED) != 0) {
        vmRaiseError(vm, "Invalid regex pattern", "regex");
        return NIL_VAL;
    }

    // Allocate space for full match + capture groups
    size_t ngroups = reg.re_nsub + 1;
    regmatch_t* matches = (regmatch_t*)malloc(sizeof(regmatch_t) * ngroups);
    if (matches == NULL) {
        regfree(&reg);
        vmRaiseError(vm, "Out of memory", "io");
        return NIL_VAL;
    }

    if (regexec(&reg, str, ngroups, matches, 0) != 0) {
        free(matches);
        regfree(&reg);
        return NIL_VAL;
    }

    int start = (int)matches[0].rm_so;
    int end = (int)matches[0].rm_eo;

    ObjMap* result = newMap(vm);
    vmPush(vm, OBJ_VAL(result));

    ObjString* matchedKey = copyString(vm, "matched", 7);
    ObjString* matchedVal = copyString(vm, str + start, end - start);
    tableSet(&result->table, matchedKey, OBJ_VAL(matchedVal));

    ObjString* startKey = copyString(vm, "start", 5);
    tableSet(&result->table, startKey, NUMBER_VAL((double)start));

    ObjString* endKey = copyString(vm, "end", 3);
    tableSet(&result->table, endKey, NUMBER_VAL((double)end));

    // Build capture groups list
    if (reg.re_nsub > 0) {
        ObjList* groups = newList(vm);
        vmPush(vm, OBJ_VAL(groups)); // GC protect

        for (size_t i = 1; i < ngroups; i++) {
            if (matches[i].rm_so == -1) {
                listAppend(vm, groups, NIL_VAL);
            } else {
                int gs = (int)matches[i].rm_so;
                int ge = (int)matches[i].rm_eo;
                ObjString* group = copyString(vm, str + gs, ge - gs);
                listAppend(vm, groups, OBJ_VAL(group));
            }
        }

        vmPop(vm); // groups
        ObjString* groupsKey = copyString(vm, "groups", 6);
        tableSet(&result->table, groupsKey, OBJ_VAL(groups));
    }

    free(matches);
    regfree(&reg);
    vmPop(vm); // result
    return OBJ_VAL(result);
}

static Value reFindAllNative(VM* vm, int argCount, Value* args) {
    (void)argCount;
    if (!IS_STRING(args[0]) || !IS_STRING(args[1])) {
        vmRaiseError(vm, "re.find_all requires string arguments", "type");
        return NIL_VAL;
    }

    regex_t reg;
    if (regcomp(&reg, AS_CSTRING(args[0]), REG_EXTENDED) != 0) {
        vmRaiseError(vm, "Invalid regex pattern", "regex");
        return NIL_VAL;
    }

    ObjList* list = newList(vm);
    vmPush(vm, OBJ_VAL(list));

    const char* cursor = AS_CSTRING(args[1]);
    regmatch_t match;

    while (regexec(&reg, cursor, 1, &match, 0) == 0) {
        int start = (int)match.rm_so;
        int end = (int)match.rm_eo;

        if (start == end) {
            if (cursor[start] == '\0') break;
            cursor += start + 1;
            continue;
        }

        ObjString* matched = copyString(vm, cursor + start, end - start);
        listAppend(vm, list, OBJ_VAL(matched));
        cursor += end;
    }

    regfree(&reg);
    vmPop(vm);
    return OBJ_VAL(list);
}

static bool growBuffer(char** buf, int* cap, int needed) {
    if (needed < *cap) return true;
    *cap = needed * 2;
    char* newBuf = (char*)realloc(*buf, *cap);
    if (newBuf == NULL) {
        free(*buf);
        *buf = NULL;
        return false;
    }
    *buf = newBuf;
    return true;
}

static Value reReplaceNative(VM* vm, int argCount, Value* args) {
    (void)argCount;
    if (!IS_STRING(args[0]) || !IS_STRING(args[1]) || !IS_STRING(args[2])) {
        vmRaiseError(vm, "re.replace requires string arguments", "type");
        return NIL_VAL;
    }
    const char* str = AS_CSTRING(args[1]);
    const char* replacement = AS_CSTRING(args[2]);
    int repLen = AS_STRING(args[2])->length;
    int strLen = AS_STRING(args[1])->length;

    regex_t reg;
    if (regcomp(&reg, AS_CSTRING(args[0]), REG_EXTENDED) != 0) {
        vmRaiseError(vm, "Invalid regex pattern", "regex");
        return NIL_VAL;
    }

    int bufCap = strLen + 64;
    char* buf = (char*)malloc(bufCap);
    if (buf == NULL) {
        regfree(&reg);
        vmRaiseError(vm, "Out of memory", "io");
        return NIL_VAL;
    }
    int bufLen = 0;

    const char* cursor = str;
    regmatch_t match;

    while (regexec(&reg, cursor, 1, &match, 0) == 0) {
        int start = (int)match.rm_so;
        int end = (int)match.rm_eo;

        if (start == end) {
            if (cursor[start] == '\0') break;
            if (!growBuffer(&buf, &bufCap, bufLen + start + 2)) {
                regfree(&reg);
                vmRaiseError(vm, "Out of memory", "io");
                return NIL_VAL;
            }
            memcpy(buf + bufLen, cursor, start + 1);
            bufLen += start + 1;
            cursor += start + 1;
            continue;
        }

        if (!growBuffer(&buf, &bufCap, bufLen + start + repLen + 1)) {
            regfree(&reg);
            vmRaiseError(vm, "Out of memory", "io");
            return NIL_VAL;
        }
        memcpy(buf + bufLen, cursor, start);
        bufLen += start;
        memcpy(buf + bufLen, replacement, repLen);
        bufLen += repLen;
        cursor += end;
    }

    int remaining = strLen - (int)(cursor - str);
    if (remaining > 0) {
        if (!growBuffer(&buf, &bufCap, bufLen + remaining + 1)) {
            regfree(&reg);
            vmRaiseError(vm, "Out of memory", "io");
            return NIL_VAL;
        }
        memcpy(buf + bufLen, cursor, remaining);
        bufLen += remaining;
    }
    buf[bufLen] = '\0';

    regfree(&reg);
    ObjString* result = copyString(vm, buf, bufLen);
    free(buf);
    return OBJ_VAL(result);
}

static Value reSplitNative(VM* vm, int argCount, Value* args) {
    (void)argCount;
    if (!IS_STRING(args[0]) || !IS_STRING(args[1])) {
        vmRaiseError(vm, "re.split requires string arguments", "type");
        return NIL_VAL;
    }
    const char* str = AS_CSTRING(args[1]);
    int strLen = AS_STRING(args[1])->length;

    regex_t reg;
    if (regcomp(&reg, AS_CSTRING(args[0]), REG_EXTENDED) != 0) {
        vmRaiseError(vm, "Invalid regex pattern", "regex");
        return NIL_VAL;
    }

    ObjList* list = newList(vm);
    vmPush(vm, OBJ_VAL(list));

    const char* cursor = str;
    regmatch_t match;

    while (regexec(&reg, cursor, 1, &match, 0) == 0) {
        int start = (int)match.rm_so;
        int end = (int)match.rm_eo;

        if (start == end) {
            if (cursor[start] == '\0') break;
            cursor += start + 1;
            continue;
        }

        ObjString* part = copyString(vm, cursor, start);
        listAppend(vm, list, OBJ_VAL(part));
        cursor += end;
    }

    int remaining = strLen - (int)(cursor - str);
    ObjString* last = copyString(vm, cursor, remaining);
    listAppend(vm, list, OBJ_VAL(last));

    regfree(&reg);
    vmPop(vm);
    return OBJ_VAL(list);
}

void registerRegexModule(VM* vm) {
    ObjMap* re = newMap(vm);
    vmPush(vm, OBJ_VAL(re));

    defineModuleNative(vm, re, "match", reMatchNative, 2);
    defineModuleNative(vm, re, "search", reSearchNative, 2);
    defineModuleNative(vm, re, "find_all", reFindAllNative, 2);
    defineModuleNative(vm, re, "replace", reReplaceNative, 3);
    defineModuleNative(vm, re, "split", reSplitNative, 2);

    ObjString* name = copyString(vm, "re", 2);
    tableSet(&vm->globals, name, OBJ_VAL(re));
    vmPop(vm);
}

#endif
