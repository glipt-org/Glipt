#ifndef glipt_scanner_h
#define glipt_scanner_h

#include "token.h"

typedef struct {
    const char* source;     // full source text
    const char* start;      // start of current lexeme
    const char* current;    // current scan position
    int line;
    int column;
    int startColumn;        // column where current lexeme started
    TokenType previous;     // previous token type (for newline suppression)
} Scanner;

void initScanner(Scanner* scanner, const char* source);
Token scanToken(Scanner* scanner);

#endif
