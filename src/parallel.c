#include "parallel.h"
#include "vm.h"
#include "object.h"
#include "process.h"

#include <pthread.h>

typedef struct {
    const char* command;
    ProcessResult result;
} ExecTask;

static void* execTaskRunner(void* arg) {
    ExecTask* task = (ExecTask*)arg;
    task->result = processExec(task->command);
    return NULL;
}

// Run multiple exec commands in parallel and return a list of results
Value parallelExec(VM* vm, int argCount, Value* args) {
    if (argCount < 1 || !IS_LIST(args[0])) return NIL_VAL;
    ObjList* commands = AS_LIST(args[0]);

    if (commands->count <= 0) return OBJ_VAL(newList(vm));
    size_t count = (size_t)commands->count;

    // Validate all commands are strings and check permissions
    for (size_t i = 0; i < count; i++) {
        if (!IS_STRING(commands->items[i])) return NIL_VAL;
        const char* cmd = AS_CSTRING(commands->items[i]);
        if (!hasPermission(&vm->permissions, PERM_EXEC, cmd)) {
            char msg[512];
            snprintf(msg, sizeof(msg), "Permission denied: exec \"%s\"", cmd);
            // Can't call raiseError from here, just return nil
            fprintf(stderr, "%s\n", msg);
            return NIL_VAL;
        }
    }

    // Spawn all tasks
    ExecTask* tasks = (ExecTask*)malloc(sizeof(ExecTask) * count);
    pthread_t* threads = (pthread_t*)malloc(sizeof(pthread_t) * count);

    for (size_t i = 0; i < count; i++) {
        tasks[i].command = AS_CSTRING(commands->items[i]);
        pthread_create(&threads[i], NULL, execTaskRunner, &tasks[i]);
    }

    // Wait for all
    for (size_t i = 0; i < count; i++) {
        pthread_join(threads[i], NULL);
    }

    // Build results list
    ObjList* results = newList(vm);
    *vm->stackTop++ = OBJ_VAL(results); // GC protect

    for (size_t i = 0; i < count; i++) {
        ObjMap* map = newMap(vm);
        *vm->stackTop++ = OBJ_VAL(map); // GC protect

        ObjString* outKey = copyString(vm, "output", 6);
        int outLen = tasks[i].result.stdoutLength;
        if (outLen > 0 && tasks[i].result.stdoutData[outLen - 1] == '\n') outLen--;
        ObjString* outVal = copyString(vm,
            tasks[i].result.stdoutData ? tasks[i].result.stdoutData : "", outLen);
        tableSet(&map->table, outKey, OBJ_VAL(outVal));

        ObjString* exitKey = copyString(vm, "exitCode", 8);
        tableSet(&map->table, exitKey, NUMBER_VAL(tasks[i].result.exitCode));

        ObjString* stderrKey = copyString(vm, "stderr", 6);
        ObjString* stderrVal = copyString(vm,
            tasks[i].result.stderrData ? tasks[i].result.stderrData : "",
            tasks[i].result.stderrLength);
        tableSet(&map->table, stderrKey, OBJ_VAL(stderrVal));

        vm->stackTop--; // unprotect map
        listAppend(vm, results, OBJ_VAL(map));
        processResultFree(&tasks[i].result);
    }

    vm->stackTop--; // unprotect results

    free(tasks);
    free(threads);

    return OBJ_VAL(results);
}
