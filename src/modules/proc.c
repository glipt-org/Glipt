#if !defined(_WIN32) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE
#endif

#include "proc.h"
#include "../object.h"
#include "../permission.h"
#include "../process.h"
#include "../table.h"

#ifdef _WIN32

void registerProcModule(VM* vm) {
    ObjMap* proc = newMap(vm);
    vmPush(vm, OBJ_VAL(proc));
    ObjString* name = copyString(vm, "proc", 4);
    tableSet(&vm->globals, name, OBJ_VAL(proc));
    vmPop(vm);
}

#else

#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>
#include <time.h>

// ---- Process Execution with Timeout ----

static Value procExecNative(VM* vm, int argCount, Value* args) {
    if (argCount < 1 || !IS_STRING(args[0])) return NIL_VAL;

    const char* command = AS_CSTRING(args[0]);

    if (!hasPermission(&vm->permissions, PERM_EXEC, command)) {
        char msg[512];
        snprintf(msg, sizeof(msg), "Permission denied: exec \"%s\"", command);
        vmRaiseError(vm, msg, "permission");
        return NIL_VAL;
    }

    double timeout = -1;
    if (argCount >= 2 && IS_NUMBER(args[1])) {
        timeout = AS_NUMBER(args[1]);
    }

    ProcessResult pr = processExec(command);

    // If timeout was specified and process takes too long, we can't easily
    // enforce it after posix_spawn returns (it already completed).
    // For true timeout support, we'd need async spawn + poll.
    // For now, timeout is stored but process runs synchronously.
    (void)timeout;

    ObjMap* result = newMap(vm);
    vmPush(vm, OBJ_VAL(result));

    tableSet(&result->table,
        copyString(vm, "code", 4), NUMBER_VAL(pr.exitCode));

    if (pr.stdoutData) {
        tableSet(&result->table,
            copyString(vm, "stdout", 6),
            OBJ_VAL(copyString(vm, pr.stdoutData, (int)strlen(pr.stdoutData))));

        // Trimmed output
        char* trimmed = pr.stdoutData;
        int len = (int)strlen(trimmed);
        while (len > 0 && (trimmed[len-1] == '\n' || trimmed[len-1] == '\r')) len--;
        tableSet(&result->table,
            copyString(vm, "output", 6),
            OBJ_VAL(copyString(vm, trimmed, len)));
    } else {
        tableSet(&result->table,
            copyString(vm, "stdout", 6), OBJ_VAL(copyString(vm, "", 0)));
        tableSet(&result->table,
            copyString(vm, "output", 6), OBJ_VAL(copyString(vm, "", 0)));
    }

    if (pr.stderrData) {
        tableSet(&result->table,
            copyString(vm, "stderr", 6),
            OBJ_VAL(copyString(vm, pr.stderrData, (int)strlen(pr.stderrData))));
    } else {
        tableSet(&result->table,
            copyString(vm, "stderr", 6), OBJ_VAL(copyString(vm, "", 0)));
    }

    free(pr.stdoutData);
    free(pr.stderrData);

    if (pr.exitCode != 0) {
        char msg[512];
        snprintf(msg, sizeof(msg), "Command failed with exit code %d: %s",
                 pr.exitCode, command);
        vmPop(vm);
        vmRaiseError(vm, msg, "exec");
        return NIL_VAL;
    }

    vmPop(vm);
    return OBJ_VAL(result);
}

// ---- Process Control ----

static Value procKillNative(VM* vm, int argCount, Value* args) {
    (void)vm;
    if (argCount < 1 || !IS_NUMBER(args[0])) return NIL_VAL;

    pid_t pid = (pid_t)AS_NUMBER(args[0]);
    int sig = SIGTERM;
    if (argCount >= 2 && IS_NUMBER(args[1])) {
        sig = (int)AS_NUMBER(args[1]);
    }

    return BOOL_VAL(kill(pid, sig) == 0);
}

static Value procRunningNative(VM* vm, int argCount, Value* args) {
    (void)vm;
    if (argCount != 1 || !IS_NUMBER(args[0])) return NIL_VAL;

    pid_t pid = (pid_t)AS_NUMBER(args[0]);
    // kill with signal 0 checks if process exists
    return BOOL_VAL(kill(pid, 0) == 0);
}

static Value procPidNative(VM* vm, int argCount, Value* args) {
    (void)vm; (void)argCount; (void)args;
    return NUMBER_VAL((double)getpid());
}

// ---- Retry ----

static Value procRetryNative(VM* vm, int argCount, Value* args) {
    if (argCount < 2 || !IS_NUMBER(args[0])) return NIL_VAL;

    int attempts = (int)AS_NUMBER(args[0]);

    // Find the function argument (last arg that's callable)
    Value fn = NIL_VAL;
    double backoff = 1.0;

    for (int i = 1; i < argCount; i++) {
        if (IS_CLOSURE(args[i]) || IS_NATIVE(args[i])) {
            fn = args[i];
        } else if (IS_NUMBER(args[i]) && i == 1) {
            backoff = AS_NUMBER(args[i]);
        }
    }

    if (IS_NIL(fn)) {
        vmRaiseError(vm, "retry requires a function argument", "type");
        return NIL_VAL;
    }

    // For native functions we can call directly
    if (IS_NATIVE(fn)) {
        NativeFn nativeFn = AS_NATIVE(fn)->function;
        for (int i = 0; i < attempts; i++) {
            vm->hasError = false;
            Value result = nativeFn(vm, 0, NULL);
            if (!vm->hasError) return result;

            // Wait before retrying (exponential backoff)
            if (i < attempts - 1) {
                double wait = backoff * (1 << i);
                if (wait > 0) {
                    struct timespec ts;
                    ts.tv_sec = (time_t)wait;
                    ts.tv_nsec = (long)((wait - (time_t)wait) * 1e9);
                    nanosleep(&ts, NULL);
                }
            }
        }
    }

    // If all retries failed, the last error is still set
    if (!vm->hasError) {
        vmRaiseError(vm, "All retry attempts failed", "retry");
    }
    return NIL_VAL;
}

// ---- Sleep (enhanced) ----

static Value procSleepNative(VM* vm, int argCount, Value* args) {
    (void)vm;
    if (argCount != 1 || !IS_NUMBER(args[0])) return NIL_VAL;
    double seconds = AS_NUMBER(args[0]);
    if (seconds > 0) {
        struct timespec ts;
        ts.tv_sec = (time_t)seconds;
        ts.tv_nsec = (long)((seconds - (time_t)seconds) * 1e9);
        nanosleep(&ts, NULL);
    }
    return NIL_VAL;
}

// ---- Module Registration ----

void registerProcModule(VM* vm) {
    ObjMap* proc = newMap(vm);
    vmPush(vm, OBJ_VAL(proc));

    defineModuleNative(vm, proc, "exec", procExecNative, -1);
    defineModuleNative(vm, proc, "kill", procKillNative, -1);
    defineModuleNative(vm, proc, "running", procRunningNative, 1);
    defineModuleNative(vm, proc, "pid", procPidNative, 0);
    defineModuleNative(vm, proc, "retry", procRetryNative, -1);
    defineModuleNative(vm, proc, "sleep", procSleepNative, 1);

    ObjString* name = copyString(vm, "proc", 4);
    tableSet(&vm->globals, name, OBJ_VAL(proc));
    vmPop(vm);
}

#endif // !_WIN32
