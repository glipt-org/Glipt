#if !defined(_WIN32) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE
#endif

#include "process.h"

#include <errno.h>
#include <ctype.h>

#ifdef _WIN32

// ---- Windows stubs (posix_spawn / pipes not available on Windows) ----

int parseCommand(const char* command, char*** argvOut) {
    (void)command;
    *argvOut = (char**)malloc(sizeof(char*));
    (*argvOut)[0] = NULL;
    return 0;
}

ProcessResult processExecv(const char** argv, int argc) {
    (void)argv; (void)argc;
    ProcessResult result;
    memset(&result, 0, sizeof(result));
    result.exitCode = -1;
    result.stderrData = (char*)malloc(32);
    strcpy(result.stderrData, "exec not supported on Windows");
    result.stderrLength = (int)strlen(result.stderrData);
    return result;
}

ProcessResult processExec(const char* command) {
    (void)command;
    return processExecv(NULL, 0);
}

void processResultFree(ProcessResult* result) {
    free(result->stdoutData);
    free(result->stderrData);
    result->stdoutData = NULL;
    result->stderrData = NULL;
}

#else

// ---- POSIX implementation ----

#include <unistd.h>
#include <sys/wait.h>
#include <spawn.h>

extern char **environ;

// ---- Command Parsing ----

// Parse a command string into an argv array, splitting on whitespace
// and respecting double quotes, single quotes, and backslash escaping.
int parseCommand(const char* command, char*** argvOut) {
    int argc = 0;
    int capacity = 8;
    char** argv = (char**)malloc(sizeof(char*) * capacity);

    const char* p = command;
    while (*p) {
        // Skip whitespace
        while (*p && isspace((unsigned char)*p)) p++;
        if (!*p) break;

        // Dynamic buffer for current argument
        int bufCap = 256;
        int len = 0;
        char* buf = (char*)malloc(bufCap);

#define APPEND_CHAR(c) \
    do { \
        if (len >= bufCap - 1) { \
            bufCap *= 2; \
            buf = (char*)realloc(buf, bufCap); \
        } \
        buf[len++] = (c); \
    } while (0)

        while (*p && !isspace((unsigned char)*p)) {
            if (*p == '"') {
                p++; // skip opening quote
                while (*p && *p != '"') {
                    if (*p == '\\' && p[1]) {
                        p++;
                    }
                    APPEND_CHAR(*p);
                    p++;
                }
                if (*p == '"') p++;
            } else if (*p == '\'') {
                p++; // skip opening quote
                while (*p && *p != '\'') {
                    APPEND_CHAR(*p);
                    p++;
                }
                if (*p == '\'') p++;
            } else if (*p == '\\' && p[1]) {
                p++;
                APPEND_CHAR(*p);
                p++;
            } else {
                APPEND_CHAR(*p);
                p++;
            }
        }

#undef APPEND_CHAR

        buf[len] = '\0';

        if (argc >= capacity - 1) { // -1 for NULL terminator
            capacity *= 2;
            argv = (char**)realloc(argv, sizeof(char*) * capacity);
        }
        argv[argc] = buf; // transfer ownership
        argc++;
    }

    argv[argc] = NULL; // NULL-terminated for execvp
    *argvOut = argv;
    return argc;
}

// ---- Process Execution ----

static char* readFd(int fd, int* outLen) {
    int capacity = 1024;
    int length = 0;
    char* buffer = (char*)malloc(capacity);

    ssize_t n;
    while ((n = read(fd, buffer + length, capacity - length)) > 0) {
        length += (int)n;
        if (length >= capacity) {
            capacity *= 2;
            buffer = (char*)realloc(buffer, capacity);
        }
    }

    buffer[length] = '\0';
    *outLen = length;
    return buffer;
}

ProcessResult processExecv(const char** argv, int argc) {
    (void)argc;
    ProcessResult result;
    memset(&result, 0, sizeof(result));

    int stdoutPipe[2];
    int stderrPipe[2];

    if (pipe(stdoutPipe) != 0 || pipe(stderrPipe) != 0) {
        result.exitCode = -1;
        result.stderrData = strdup("Failed to create pipes");
        result.stderrLength = (int)strlen(result.stderrData);
        return result;
    }

    // Set up file actions for posix_spawn
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_addclose(&actions, stdoutPipe[0]);
    posix_spawn_file_actions_addclose(&actions, stderrPipe[0]);
    posix_spawn_file_actions_adddup2(&actions, stdoutPipe[1], STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&actions, stderrPipe[1], STDERR_FILENO);
    posix_spawn_file_actions_addclose(&actions, stdoutPipe[1]);
    posix_spawn_file_actions_addclose(&actions, stderrPipe[1]);

    pid_t pid;
    int status = posix_spawnp(&pid, argv[0], &actions, NULL,
                               (char* const*)argv, environ);

    posix_spawn_file_actions_destroy(&actions);

    if (status != 0) {
        close(stdoutPipe[0]); close(stdoutPipe[1]);
        close(stderrPipe[0]); close(stderrPipe[1]);
        result.exitCode = -1;
        char errMsg[256];
        snprintf(errMsg, sizeof(errMsg), "Failed to spawn '%s': %s",
                 argv[0], strerror(status));
        result.stderrData = strdup(errMsg);
        result.stderrLength = (int)strlen(result.stderrData);
        return result;
    }

    // Close write ends in parent
    close(stdoutPipe[1]);
    close(stderrPipe[1]);

    // Read stdout and stderr
    result.stdoutData = readFd(stdoutPipe[0], &result.stdoutLength);
    result.stderrData = readFd(stderrPipe[0], &result.stderrLength);

    close(stdoutPipe[0]);
    close(stderrPipe[0]);

    // Wait for child
    int wstatus;
    waitpid(pid, &wstatus, 0);
    if (WIFEXITED(wstatus)) {
        result.exitCode = WEXITSTATUS(wstatus);
    } else {
        result.exitCode = -1;
    }

    return result;
}

ProcessResult processExec(const char* command) {
    char** argv;
    int argc = parseCommand(command, &argv);

    if (argc == 0) {
        ProcessResult result;
        memset(&result, 0, sizeof(result));
        result.exitCode = -1;
        result.stderrData = strdup("Empty command");
        result.stderrLength = (int)strlen(result.stderrData);
        free(argv);
        return result;
    }

    ProcessResult result = processExecv((const char**)argv, argc);

    for (int i = 0; i < argc; i++) {
        free(argv[i]);
    }
    free(argv);

    return result;
}

void processResultFree(ProcessResult* result) {
    free(result->stdoutData);
    free(result->stderrData);
    result->stdoutData = NULL;
    result->stderrData = NULL;
}

#endif // _WIN32
