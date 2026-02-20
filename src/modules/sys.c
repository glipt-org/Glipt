#if !defined(_WIN32) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE
#endif

#include "sys.h"
#include "../object.h"
#include "../table.h"

#ifdef _WIN32

void registerSysModule(VM* vm) {
    ObjMap* sys = newMap(vm);
    vmPush(vm, OBJ_VAL(sys));
    ObjString* name = copyString(vm, "sys", 3);
    tableSet(&vm->globals, name, OBJ_VAL(sys));
    vmPop(vm);
}

#else

#include <unistd.h>
#include <sys/utsname.h>
#include <time.h>
#include <pwd.h>

// ---- Process Info ----

static Value sysPidNative(VM* vm, int argCount, Value* args) {
    (void)vm; (void)argCount; (void)args;
    return NUMBER_VAL((double)getpid());
}

static Value sysPpidNative(VM* vm, int argCount, Value* args) {
    (void)vm; (void)argCount; (void)args;
    return NUMBER_VAL((double)getppid());
}

static Value sysUidNative(VM* vm, int argCount, Value* args) {
    (void)vm; (void)argCount; (void)args;
    return NUMBER_VAL((double)getuid());
}

static Value sysGidNative(VM* vm, int argCount, Value* args) {
    (void)vm; (void)argCount; (void)args;
    return NUMBER_VAL((double)getgid());
}

static Value sysHostnameNative(VM* vm, int argCount, Value* args) {
    (void)argCount; (void)args;
    char hostname[256];
    if (gethostname(hostname, sizeof(hostname)) != 0) return NIL_VAL;
    return OBJ_VAL(copyString(vm, hostname, (int)strlen(hostname)));
}

static Value sysUsernameNative(VM* vm, int argCount, Value* args) {
    (void)argCount; (void)args;
    struct passwd* pw = getpwuid(getuid());
    if (!pw) return NIL_VAL;
    return OBJ_VAL(copyString(vm, pw->pw_name, (int)strlen(pw->pw_name)));
}

// ---- Platform Info ----

static Value sysPlatformNative(VM* vm, int argCount, Value* args) {
    (void)argCount; (void)args;
    struct utsname info;
    if (uname(&info) != 0) return NIL_VAL;

    // Lowercase the sysname
    char platform[64];
    int i;
    for (i = 0; info.sysname[i] && i < 63; i++) {
        platform[i] = (char)(info.sysname[i] >= 'A' && info.sysname[i] <= 'Z'
            ? info.sysname[i] + 32 : info.sysname[i]);
    }
    platform[i] = '\0';

    return OBJ_VAL(copyString(vm, platform, i));
}

static Value sysArchNative(VM* vm, int argCount, Value* args) {
    (void)argCount; (void)args;
    struct utsname info;
    if (uname(&info) != 0) return NIL_VAL;
    return OBJ_VAL(copyString(vm, info.machine, (int)strlen(info.machine)));
}

// ---- Resource Info ----

static Value sysCpuCountNative(VM* vm, int argCount, Value* args) {
    (void)vm; (void)argCount; (void)args;
#ifdef _SC_NPROCESSORS_ONLN
    long cpus = sysconf(_SC_NPROCESSORS_ONLN);
    return NUMBER_VAL(cpus > 0 ? (double)cpus : 1.0);
#else
    return NUMBER_VAL(1.0);
#endif
}

static Value sysClockNative(VM* vm, int argCount, Value* args) {
    (void)vm; (void)argCount; (void)args;
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return NUMBER_VAL((double)ts.tv_sec + (double)ts.tv_nsec / 1e9);
}

static Value sysTimeNative(VM* vm, int argCount, Value* args) {
    (void)vm; (void)argCount; (void)args;
    return NUMBER_VAL((double)time(NULL));
}

static Value sysCwdNative(VM* vm, int argCount, Value* args) {
    (void)argCount; (void)args;
    char cwd[1024];
    if (getcwd(cwd, sizeof(cwd)) == NULL) return NIL_VAL;
    return OBJ_VAL(copyString(vm, cwd, (int)strlen(cwd)));
}

static Value sysArgsNative(VM* vm, int argCount, Value* args) {
    (void)argCount; (void)args;
    ObjList* list = newList(vm);
    vmPush(vm, OBJ_VAL(list));
    for (int i = 0; i < vm->scriptArgc; i++) {
        int len = (int)strlen(vm->scriptArgv[i]);
        listAppend(vm, list, OBJ_VAL(copyString(vm, vm->scriptArgv[i], len)));
    }
    vmPop(vm);
    return OBJ_VAL(list);
}

// ---- Module Registration ----

void registerSysModule(VM* vm) {
    ObjMap* sys = newMap(vm);
    vmPush(vm, OBJ_VAL(sys));

    // Process info
    defineModuleNative(vm, sys, "pid", sysPidNative, 0);
    defineModuleNative(vm, sys, "ppid", sysPpidNative, 0);
    defineModuleNative(vm, sys, "uid", sysUidNative, 0);
    defineModuleNative(vm, sys, "gid", sysGidNative, 0);
    defineModuleNative(vm, sys, "hostname", sysHostnameNative, 0);
    defineModuleNative(vm, sys, "username", sysUsernameNative, 0);

    // Platform
    defineModuleNative(vm, sys, "platform", sysPlatformNative, 0);
    defineModuleNative(vm, sys, "arch", sysArchNative, 0);
    defineModuleNative(vm, sys, "cpu_count", sysCpuCountNative, 0);

    // Time
    defineModuleNative(vm, sys, "clock", sysClockNative, 0);
    defineModuleNative(vm, sys, "time", sysTimeNative, 0);

    // System
    defineModuleNative(vm, sys, "cwd", sysCwdNative, 0);
    defineModuleNative(vm, sys, "args", sysArgsNative, 0);

    ObjString* name = copyString(vm, "sys", 3);
    tableSet(&vm->globals, name, OBJ_VAL(sys));
    vmPop(vm);
}

#endif // !_WIN32
