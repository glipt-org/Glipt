#ifndef glipt_process_h
#define glipt_process_h

#include "common.h"

typedef struct {
    int exitCode;
    char* stdoutData;
    int stdoutLength;
    char* stderrData;
    int stderrLength;
} ProcessResult;

// Execute a command string, splitting by whitespace (respecting quotes).
// Returns a ProcessResult. Caller must free stdoutData and stderrData.
ProcessResult processExec(const char* command);

// Execute with explicit argv array (no shell).
ProcessResult processExecv(const char** argv, int argc);

// Free the data inside a ProcessResult (but not the struct itself).
void processResultFree(ProcessResult* result);

// Parse a command string into argv array. Returns argc.
// Caller must free each element and the array.
int parseCommand(const char* command, char*** argvOut);

#endif
