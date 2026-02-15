#include "common.h"
#include "scanner.h"
#include "parser.h"
#include "ast.h"
#include "vm.h"
#include "compiler.h"
#include "debug.h"

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

    printf("Glipt 0.1.0 REPL (type 'exit' to quit)\n");

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
    printf("  version            Show version\n");
    printf("  help               Show this help\n");
}

static void printVersion(void) {
    printf("Glipt 0.1.0\n");
    printf("Glue + Script - Process Orchestration Language\n");
}

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
        for (int i = 2; i < argc; i++) {
            if (strcmp(argv[i], "--allow-all") == 0) {
                allowAll = true;
            } else if (scriptPath == NULL) {
                scriptPath = argv[i];
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
        if (allowAll) {
            vm.permissions.allowAll = true;
        }
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
