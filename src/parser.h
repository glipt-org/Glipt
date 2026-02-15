#ifndef glipt_parser_h
#define glipt_parser_h

#include "ast.h"
#include "scanner.h"

typedef struct {
    Scanner scanner;
    Token current;
    Token previous;
    Arena* arena;
    bool hadError;
    bool panicMode;
} Parser;

// Parse source code into an AST. Returns NULL on error.
// The arena must be initialized before calling.
AstNode* parse(const char* source, Arena* arena);

#endif
