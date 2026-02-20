#if !defined(_WIN32) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE
#endif

#include "fs.h"
#include "../object.h"
#include "../permission.h"
#include "../table.h"

#ifdef _WIN32

// Windows: register fs module with no functions for now
void registerFsModule(VM* vm) {
    ObjMap* fs = newMap(vm);
    vmPush(vm, OBJ_VAL(fs));
    ObjString* name = copyString(vm, "fs", 2);
    tableSet(&vm->globals, name, OBJ_VAL(fs));
    vmPop(vm);
}

#else

#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <libgen.h>
#include <limits.h>

// ---- Directory Operations ----

static Value fsListNative(VM* vm, int argCount, Value* args) {
    if (argCount != 1 || !IS_STRING(args[0])) return NIL_VAL;

    const char* path = AS_CSTRING(args[0]);

    if (!hasPermission(&vm->permissions, PERM_READ, path)) {
        vmRaiseError(vm, "Permission denied: read", "permission");
        return NIL_VAL;
    }

    DIR* dir = opendir(path);
    if (!dir) {
        vmRaiseError(vm, "Could not open directory", "io");
        return NIL_VAL;
    }

    ObjList* list = newList(vm);
    vmPush(vm, OBJ_VAL(list));

    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;
        ObjString* name = copyString(vm, entry->d_name, (int)strlen(entry->d_name));
        listAppend(vm, list, OBJ_VAL(name));
    }

    closedir(dir);
    vmPop(vm);
    return OBJ_VAL(list);
}

static Value fsMkdirNative(VM* vm, int argCount, Value* args) {
    if (argCount < 1 || !IS_STRING(args[0])) return NIL_VAL;

    const char* path = AS_CSTRING(args[0]);

    if (!hasPermission(&vm->permissions, PERM_WRITE, path)) {
        vmRaiseError(vm, "Permission denied: write", "permission");
        return NIL_VAL;
    }

    int result = mkdir(path, 0755);
    return BOOL_VAL(result == 0);
}

static Value fsRmdirNative(VM* vm, int argCount, Value* args) {
    if (argCount < 1 || !IS_STRING(args[0])) return NIL_VAL;

    const char* path = AS_CSTRING(args[0]);

    if (!hasPermission(&vm->permissions, PERM_WRITE, path)) {
        vmRaiseError(vm, "Permission denied: write", "permission");
        return NIL_VAL;
    }

    int result = rmdir(path);
    return BOOL_VAL(result == 0);
}

static Value fsExistsNative(VM* vm, int argCount, Value* args) {
    (void)vm;
    if (argCount != 1 || !IS_STRING(args[0])) return NIL_VAL;
    return BOOL_VAL(access(AS_CSTRING(args[0]), F_OK) == 0);
}

static Value fsIsfileNative(VM* vm, int argCount, Value* args) {
    (void)vm;
    if (argCount != 1 || !IS_STRING(args[0])) return NIL_VAL;
    struct stat st;
    if (stat(AS_CSTRING(args[0]), &st) != 0) return BOOL_VAL(false);
    return BOOL_VAL(S_ISREG(st.st_mode));
}

static Value fsIsdirNative(VM* vm, int argCount, Value* args) {
    (void)vm;
    if (argCount != 1 || !IS_STRING(args[0])) return NIL_VAL;
    struct stat st;
    if (stat(AS_CSTRING(args[0]), &st) != 0) return BOOL_VAL(false);
    return BOOL_VAL(S_ISDIR(st.st_mode));
}

// ---- File Metadata ----

static Value fsStatNative(VM* vm, int argCount, Value* args) {
    if (argCount != 1 || !IS_STRING(args[0])) return NIL_VAL;

    const char* path = AS_CSTRING(args[0]);

    if (!hasPermission(&vm->permissions, PERM_READ, path)) {
        vmRaiseError(vm, "Permission denied: read", "permission");
        return NIL_VAL;
    }

    struct stat st;
    if (stat(path, &st) != 0) return NIL_VAL;

    ObjMap* map = newMap(vm);
    vmPush(vm, OBJ_VAL(map));

    tableSet(&map->table,
        copyString(vm, "size", 4), NUMBER_VAL((double)st.st_size));
    tableSet(&map->table,
        copyString(vm, "mtime", 5), NUMBER_VAL((double)st.st_mtime));
    tableSet(&map->table,
        copyString(vm, "mode", 4), NUMBER_VAL((double)st.st_mode));
    tableSet(&map->table,
        copyString(vm, "isFile", 6), BOOL_VAL(S_ISREG(st.st_mode)));
    tableSet(&map->table,
        copyString(vm, "isDir", 5), BOOL_VAL(S_ISDIR(st.st_mode)));

    vmPop(vm);
    return OBJ_VAL(map);
}

static Value fsSizeNative(VM* vm, int argCount, Value* args) {
    if (argCount != 1 || !IS_STRING(args[0])) return NIL_VAL;

    if (!hasPermission(&vm->permissions, PERM_READ, AS_CSTRING(args[0]))) {
        vmRaiseError(vm, "Permission denied: read", "permission");
        return NIL_VAL;
    }

    struct stat st;
    if (stat(AS_CSTRING(args[0]), &st) != 0) return NIL_VAL;
    return NUMBER_VAL((double)st.st_size);
}

// ---- Path Utilities ----

static Value fsJoinNative(VM* vm, int argCount, Value* args) {
    if (argCount < 2) return NIL_VAL;

    char result[PATH_MAX];
    result[0] = '\0';

    for (int i = 0; i < argCount; i++) {
        if (!IS_STRING(args[i])) return NIL_VAL;
        const char* part = AS_CSTRING(args[i]);

        if (i > 0 && result[strlen(result) - 1] != '/') {
            strncat(result, "/", PATH_MAX - strlen(result) - 1);
        }
        strncat(result, part, PATH_MAX - strlen(result) - 1);
    }

    return OBJ_VAL(copyString(vm, result, (int)strlen(result)));
}

static Value fsDirnameNative(VM* vm, int argCount, Value* args) {
    if (argCount != 1 || !IS_STRING(args[0])) return NIL_VAL;

    // dirname may modify input, so copy it
    char buf[PATH_MAX];
    strncpy(buf, AS_CSTRING(args[0]), PATH_MAX - 1);
    buf[PATH_MAX - 1] = '\0';

    char* dir = dirname(buf);
    return OBJ_VAL(copyString(vm, dir, (int)strlen(dir)));
}

static Value fsBasenameNative(VM* vm, int argCount, Value* args) {
    if (argCount != 1 || !IS_STRING(args[0])) return NIL_VAL;

    char buf[PATH_MAX];
    strncpy(buf, AS_CSTRING(args[0]), PATH_MAX - 1);
    buf[PATH_MAX - 1] = '\0';

    char* base = basename(buf);
    return OBJ_VAL(copyString(vm, base, (int)strlen(base)));
}

static Value fsExtnameNative(VM* vm, int argCount, Value* args) {
    if (argCount != 1 || !IS_STRING(args[0])) return NIL_VAL;

    const char* path = AS_CSTRING(args[0]);
    const char* dot = strrchr(path, '.');
    if (!dot || dot == path) return OBJ_VAL(copyString(vm, "", 0));
    return OBJ_VAL(copyString(vm, dot, (int)strlen(dot)));
}

static Value fsAbsoluteNative(VM* vm, int argCount, Value* args) {
    if (argCount != 1 || !IS_STRING(args[0])) return NIL_VAL;

    char resolved[PATH_MAX];
    if (realpath(AS_CSTRING(args[0]), resolved) == NULL) return NIL_VAL;
    return OBJ_VAL(copyString(vm, resolved, (int)strlen(resolved)));
}

// ---- File Operations ----

static Value fsCopyNative(VM* vm, int argCount, Value* args) {
    if (argCount != 2 || !IS_STRING(args[0]) || !IS_STRING(args[1])) return NIL_VAL;

    const char* src = AS_CSTRING(args[0]);
    const char* dest = AS_CSTRING(args[1]);

    if (!hasPermission(&vm->permissions, PERM_READ, src)) {
        vmRaiseError(vm, "Permission denied: read", "permission");
        return NIL_VAL;
    }
    if (!hasPermission(&vm->permissions, PERM_WRITE, dest)) {
        vmRaiseError(vm, "Permission denied: write", "permission");
        return NIL_VAL;
    }

    FILE* in = fopen(src, "rb");
    if (!in) return BOOL_VAL(false);

    FILE* out = fopen(dest, "wb");
    if (!out) { fclose(in); return BOOL_VAL(false); }

    char buf[8192];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
        if (fwrite(buf, 1, n, out) != n) {
            fclose(in);
            fclose(out);
            return BOOL_VAL(false);
        }
    }

    fclose(in);
    fclose(out);
    return BOOL_VAL(true);
}

static Value fsMoveNative(VM* vm, int argCount, Value* args) {
    if (argCount != 2 || !IS_STRING(args[0]) || !IS_STRING(args[1])) return NIL_VAL;

    const char* src = AS_CSTRING(args[0]);
    const char* dest = AS_CSTRING(args[1]);

    if (!hasPermission(&vm->permissions, PERM_WRITE, src)) {
        vmRaiseError(vm, "Permission denied: write", "permission");
        return NIL_VAL;
    }
    if (!hasPermission(&vm->permissions, PERM_WRITE, dest)) {
        vmRaiseError(vm, "Permission denied: write", "permission");
        return NIL_VAL;
    }

    return BOOL_VAL(rename(src, dest) == 0);
}

static Value fsRemoveNative(VM* vm, int argCount, Value* args) {
    if (argCount != 1 || !IS_STRING(args[0])) return NIL_VAL;

    const char* path = AS_CSTRING(args[0]);

    if (!hasPermission(&vm->permissions, PERM_WRITE, path)) {
        vmRaiseError(vm, "Permission denied: write", "permission");
        return NIL_VAL;
    }

    return BOOL_VAL(remove(path) == 0);
}

// ---- Module Registration ----

void registerFsModule(VM* vm) {
    ObjMap* fs = newMap(vm);
    vmPush(vm, OBJ_VAL(fs));

    // Directory operations
    defineModuleNative(vm, fs, "list", fsListNative, 1);
    defineModuleNative(vm, fs, "mkdir", fsMkdirNative, -1);
    defineModuleNative(vm, fs, "rmdir", fsRmdirNative, -1);
    defineModuleNative(vm, fs, "exists", fsExistsNative, 1);
    defineModuleNative(vm, fs, "isfile", fsIsfileNative, 1);
    defineModuleNative(vm, fs, "isdir", fsIsdirNative, 1);

    // Metadata
    defineModuleNative(vm, fs, "stat", fsStatNative, 1);
    defineModuleNative(vm, fs, "size", fsSizeNative, 1);

    // Path utilities
    defineModuleNative(vm, fs, "join", fsJoinNative, -1);
    defineModuleNative(vm, fs, "dirname", fsDirnameNative, 1);
    defineModuleNative(vm, fs, "basename", fsBasenameNative, 1);
    defineModuleNative(vm, fs, "extname", fsExtnameNative, 1);
    defineModuleNative(vm, fs, "absolute", fsAbsoluteNative, 1);

    // File operations
    defineModuleNative(vm, fs, "copy", fsCopyNative, 2);
    defineModuleNative(vm, fs, "move", fsMoveNative, 2);
    defineModuleNative(vm, fs, "remove", fsRemoveNative, 1);

    // Register as global
    ObjString* name = copyString(vm, "fs", 2);
    tableSet(&vm->globals, name, OBJ_VAL(fs));
    vmPop(vm);
}

#endif // !_WIN32
