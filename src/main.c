#if !defined(_WIN32) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE
#endif

#include "common.h"
#include "scanner.h"
#include "parser.h"
#include "ast.h"
#include "vm.h"
#include "compiler.h"
#include "debug.h"
#include "version.h"
#include "process.h"

#include <time.h>

#ifndef _WIN32
#include <sys/stat.h>
#include <unistd.h>
#endif

static char* readFile(const char* path) {
    FILE* file = fopen(path, "rb");
    if (file == NULL) {
        fprintf(stderr, "Error: Could not open file '%s'.\n", path);
        return NULL;
    }

    fseek(file, 0L, SEEK_END);
    size_t fileSize = ftell(file);
    rewind(file);

    char* buffer = (char*)malloc(fileSize + 1);
    if (buffer == NULL) {
        fprintf(stderr, "Error: Not enough memory to read '%s'.\n", path);
        fclose(file);
        return NULL;
    }

    size_t bytesRead = fread(buffer, sizeof(char), fileSize, file);
    if (bytesRead < fileSize) {
        fprintf(stderr, "Error: Could not read file '%s'.\n", path);
        free(buffer);
        fclose(file);
        return NULL;
    }

    buffer[bytesRead] = '\0';
    fclose(file);
    return buffer;
}

// Tokenize and print tokens for testing
static void runScanner(const char* source) {
    Scanner scanner;
    initScanner(&scanner, source);

    int line = -1;
    for (;;) {
        Token token = scanToken(&scanner);
        if (token.line != line) {
            printf("%4d ", token.line);
            line = token.line;
        } else {
            printf("   | ");
        }
        printf("%-16s '%.*s'\n", tokenTypeName(token.type),
               token.length, token.start);

        if (token.type == TOKEN_EOF) break;
    }
}

// Parse and print AST for testing
static int runParser(const char* source) {
    Arena arena;
    arenaInit(&arena, 0);

    AstNode* program = parse(source, &arena);
    if (program == NULL) {
        fprintf(stderr, "Parse failed.\n");
        arenaFree(&arena);
        return 1;
    }

    astPrint(program, 0);
    arenaFree(&arena);
    return 0;
}

static void runRepl(void) {
    VM vm;
    initVM(&vm);
    vm.permissions.allowAll = true; // REPL has all permissions

    printf("Glipt %s REPL (type 'exit' to quit)\n", GLIPT_VERSION);

    char line[4096];
    char buffer[65536];
    int bufLen = 0;
    int braceDepth = 0;

    for (;;) {
        if (braceDepth > 0) {
            printf("... ");
        } else {
            printf(">>> ");
        }
        fflush(stdout);

        if (!fgets(line, sizeof(line), stdin)) {
            printf("\n");
            break;
        }

        // Check for exit
        int lineLen = (int)strlen(line);
        if (lineLen > 0 && line[lineLen - 1] == '\n') {
            line[--lineLen] = '\0';
        }

        // Trim whitespace for command check
        char* trimmed = line;
        while (*trimmed == ' ' || *trimmed == '\t') trimmed++;
        if (strcmp(trimmed, "exit") == 0 || strcmp(trimmed, "quit") == 0) {
            break;
        }

        // Track brace depth for multi-line blocks
        for (int i = 0; i < lineLen; i++) {
            if (line[i] == '{') braceDepth++;
            else if (line[i] == '}') braceDepth--;
        }

        // Append to buffer
        if (bufLen + lineLen + 2 < (int)sizeof(buffer)) {
            memcpy(buffer + bufLen, line, lineLen);
            bufLen += lineLen;
            buffer[bufLen++] = '\n';
            buffer[bufLen] = '\0';
        }

        // If braces balanced, execute
        if (braceDepth <= 0) {
            braceDepth = 0;
            interpret(&vm, buffer);
            bufLen = 0;
            buffer[0] = '\0';
        }
    }

    freeVM(&vm);
}

static void printUsage(void) {
    printf("Usage: glipt <command> [options]\n\n");
    printf("Commands:\n");
    printf("  run <script>       Run a .glipt script\n");
    printf("  run --allow-all    Run with all permissions granted\n");
    printf("  repl               Interactive REPL\n");
    printf("  check <script>     Syntax check only\n");
    printf("  disasm <script>    Show bytecode disassembly\n");
    printf("  ast <script>       Show AST (debug)\n");
    printf("  tokens <script>    Show token stream (debug)\n");
    printf("  update             Check for updates\n");
    printf("  version            Show version\n");
    printf("  help               Show this help\n");
}

static void printVersion(void) {
    printf("Glipt %s\n", GLIPT_VERSION);
    printf("Glue + Script - Process Orchestration Language\n");
}

// ---- Update Checker ----

// Simple semver compare: returns >0 if a > b, 0 if equal, <0 if a < b
// Handles "vX.Y.Z" or "X.Y.Z" format
static int compareVersions(const char* a, const char* b) {
    // Skip leading 'v'
    if (*a == 'v') a++;
    if (*b == 'v') b++;

    int a1 = 0, a2 = 0, a3 = 0;
    int b1 = 0, b2 = 0, b3 = 0;
    sscanf(a, "%d.%d.%d", &a1, &a2, &a3);
    sscanf(b, "%d.%d.%d", &b1, &b2, &b3);

    if (a1 != b1) return a1 - b1;
    if (a2 != b2) return a2 - b2;
    return a3 - b3;
}

// Extract "tag_name" value from JSON (simple, no full parser needed)
static bool extractTagName(const char* json, char* tag, int tagSize) {
    const char* key = strstr(json, "\"tag_name\"");
    if (!key) return false;
    key += 10; // skip "tag_name"
    while (*key == ' ' || *key == ':') key++;
    if (*key != '"') return false;
    key++; // skip opening quote
    int i = 0;
    while (*key && *key != '"' && i < tagSize - 1) {
        tag[i++] = *key++;
    }
    tag[i] = '\0';
    return i > 0;
}

static void checkForUpdate(bool verbose) {
    const char* argv[] = {
        "curl", "-s", "-m", "5",
        "https://api.github.com/repos/" GLIPT_REPO "/releases/latest",
        NULL
    };

    ProcessResult proc = processExecv(argv, 5);

    if (proc.exitCode != 0 || proc.stdoutLength == 0) {
        if (verbose) {
            fprintf(stderr, "Could not check for updates (is curl installed?).\n");
        }
        processResultFree(&proc);
        return;
    }

    char latestTag[64];
    if (!extractTagName(proc.stdoutData, latestTag, sizeof(latestTag))) {
        if (verbose) {
            fprintf(stderr, "Could not parse release info.\n");
        }
        processResultFree(&proc);
        return;
    }

    processResultFree(&proc);

    if (compareVersions(latestTag, GLIPT_VERSION) > 0) {
        fprintf(stderr, "\nGlipt %s is available (you have %s).\n",
                latestTag, GLIPT_VERSION);
        fprintf(stderr, "Update: https://github.com/" GLIPT_REPO "/releases/latest\n\n");
    } else if (verbose) {
        printf("Glipt %s is up to date.\n", GLIPT_VERSION);
    }
}

#ifndef _WIN32
// Get path to ~/.glipt/ config directory, creating it if needed
static bool getConfigDir(char* buf, int size) {
    const char* home = getenv("HOME");
    if (!home) return false;
    int len = snprintf(buf, size, "%s/.glipt", home);
    if (len >= size) return false;
    mkdir(buf, 0755); // create if doesn't exist, ignore errors
    return true;
}

// Returns true if we should auto-check (>24 hours since last check)
static bool shouldAutoCheck(void) {
    char dir[512];
    if (!getConfigDir(dir, sizeof(dir))) return false;

    char path[600];
    snprintf(path, sizeof(path), "%s/last_update_check", dir);

    struct stat st;
    if (stat(path, &st) != 0) return true; // file doesn't exist, first run

    time_t now = time(NULL);
    return (now - st.st_mtime) > 86400; // >24 hours
}

static void touchCheckFile(void) {
    char dir[512];
    if (!getConfigDir(dir, sizeof(dir))) return;

    char path[600];
    snprintf(path, sizeof(path), "%s/last_update_check", dir);

    FILE* f = fopen(path, "w");
    if (f) fclose(f);
}

// Auto-check in background (fork + check + exit)
static void autoCheckInBackground(void) {
    if (!shouldAutoCheck()) return;

    touchCheckFile(); // touch immediately to prevent race

    pid_t pid = fork();
    if (pid == 0) {
        // Child process: check and exit
        checkForUpdate(false);
        _exit(0);
    }
    // Parent continues immediately (child will be reaped by init)
}
#endif

int main(int argc, char* argv[]) {
    if (argc < 2) {
        runRepl();
        return 0;
    }

    if (strcmp(argv[1], "help") == 0 || strcmp(argv[1], "--help") == 0) {
        printUsage();
        return 0;
    }

    if (strcmp(argv[1], "version") == 0 || strcmp(argv[1], "--version") == 0) {
        printVersion();
        return 0;
    }

    if (strcmp(argv[1], "update") == 0) {
        checkForUpdate(true);
        return 0;
    }

    if (strcmp(argv[1], "tokens") == 0) {
        if (argc < 3) {
            fprintf(stderr, "Error: 'tokens' command requires a script path.\n");
            return 1;
        }
        char* source = readFile(argv[2]);
        if (source == NULL) return 1;
        runScanner(source);
        free(source);
        return 0;
    }

    if (strcmp(argv[1], "ast") == 0) {
        if (argc < 3) {
            fprintf(stderr, "Error: 'ast' command requires a script path.\n");
            return 1;
        }
        char* source = readFile(argv[2]);
        if (source == NULL) return 1;
        int result = runParser(source);
        free(source);
        return result;
    }

    if (strcmp(argv[1], "check") == 0) {
        if (argc < 3) {
            fprintf(stderr, "Error: 'check' command requires a script path.\n");
            return 1;
        }
        char* source = readFile(argv[2]);
        if (source == NULL) return 1;
        Arena arena;
        arenaInit(&arena, 0);
        AstNode* program = parse(source, &arena);
        if (program == NULL) {
            fprintf(stderr, "Syntax errors found.\n");
            arenaFree(&arena);
            free(source);
            return 1;
        }
        printf("OK: %d top-level statements parsed.\n", program->as.block.count);
        arenaFree(&arena);
        free(source);
        return 0;
    }

    if (strcmp(argv[1], "disasm") == 0) {
        if (argc < 3) {
            fprintf(stderr, "Error: 'disasm' command requires a script path.\n");
            return 1;
        }
        char* source = readFile(argv[2]);
        if (source == NULL) return 1;
        VM vm;
        initVM(&vm);
        ObjFunction* fn = compile(&vm, source);
        if (fn != NULL) {
            disassembleChunk(&fn->chunk, "<script>");
        } else {
            fprintf(stderr, "Compilation failed.\n");
        }
        freeVM(&vm);
        free(source);
        return fn == NULL ? 1 : 0;
    }

    if (strcmp(argv[1], "run") == 0) {
        if (argc < 3) {
            fprintf(stderr, "Error: 'run' command requires a script path.\n");
            return 1;
        }

        // Parse flags
        bool allowAll = false;
        const char* scriptPath = NULL;
        int scriptArgStart = -1;
        for (int i = 2; i < argc; i++) {
            if (strcmp(argv[i], "--allow-all") == 0) {
                allowAll = true;
            } else if (scriptPath == NULL) {
                scriptPath = argv[i];
                scriptArgStart = i + 1;
            }
        }
        if (scriptPath == NULL) {
            fprintf(stderr, "Error: 'run' command requires a script path.\n");
            return 1;
        }

        char* source = readFile(scriptPath);
        if (source == NULL) return 1;
        VM vm;
        initVM(&vm);
        vm.scriptPath = scriptPath;
        if (allowAll) {
            vm.permissions.allowAll = true;
        }
        if (scriptArgStart >= 0 && scriptArgStart < argc) {
            vm.scriptArgc = argc - scriptArgStart;
            vm.scriptArgv = &argv[scriptArgStart];
        }
#ifndef _WIN32
        autoCheckInBackground();
#endif
        InterpretResult result = interpret(&vm, source);
        freeVM(&vm);
        free(source);
        switch (result) {
            case INTERPRET_OK:            return 0;
            case INTERPRET_COMPILE_ERROR: return 65;
            case INTERPRET_RUNTIME_ERROR: return 70;
        }
        return 0;
    }

    if (strcmp(argv[1], "repl") == 0) {
        runRepl();
        return 0;
    }

    fprintf(stderr, "Error: Unknown command '%s'.\n", argv[1]);
    printUsage();
    return 1;
}
