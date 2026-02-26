#include "scanner.h"
#include "common.h"

void initScanner(Scanner* scanner, const char* source) {
    scanner->source = source;
    scanner->start = source;
    scanner->current = source;
    scanner->line = 1;
    scanner->column = 1;
    scanner->startColumn = 1;
    scanner->previous = TOKEN_EOF;
}

static bool isAtEnd(Scanner* scanner) {
    return *scanner->current == '\0';
}

static char advance(Scanner* scanner) {
    scanner->current++;
    scanner->column++;
    return scanner->current[-1];
}

static char peek(Scanner* scanner) {
    return *scanner->current;
}

static char peekNext(Scanner* scanner) {
    if (isAtEnd(scanner)) return '\0';
    return scanner->current[1];
}

static bool match(Scanner* scanner, char expected) {
    if (isAtEnd(scanner)) return false;
    if (*scanner->current != expected) return false;
    scanner->current++;
    scanner->column++;
    return true;
}

static Token makeToken(Scanner* scanner, TokenType type) {
    Token token;
    token.type = type;
    token.start = scanner->start;
    token.length = (int)(scanner->current - scanner->start);
    token.line = scanner->line;
    token.column = scanner->startColumn;
    scanner->previous = type;
    return token;
}

static Token errorToken(Scanner* scanner, const char* message) {
    Token token;
    token.type = TOKEN_ERROR;
    token.start = message;
    token.length = (int)strlen(message);
    token.line = scanner->line;
    token.column = scanner->startColumn;
    return token;
}

static void skipWhitespace(Scanner* scanner) {
    for (;;) {
        char c = peek(scanner);
        switch (c) {
            case ' ':
            case '\r':
            case '\t':
                advance(scanner);
                break;
            case '#':
                // Comment: skip to end of line
                while (peek(scanner) != '\n' && !isAtEnd(scanner)) {
                    advance(scanner);
                }
                break;
            default:
                return;
        }
    }
}

// Returns true if a newline after the given token type should be suppressed
// (i.e., the token expects a continuation on the next line).
static bool suppressNewlineAfter(TokenType type) {
    switch (type) {
        case TOKEN_LEFT_PAREN:
        case TOKEN_LEFT_BRACE:
        case TOKEN_LEFT_BRACKET:
        case TOKEN_COMMA:
        case TOKEN_COLON:
        case TOKEN_PLUS:
        case TOKEN_MINUS:
        case TOKEN_STAR:
        case TOKEN_SLASH:
        case TOKEN_PERCENT:
        case TOKEN_EQUAL:
        case TOKEN_BANG_EQUAL:
        case TOKEN_EQUAL_EQUAL:
        case TOKEN_GREATER:
        case TOKEN_GREATER_EQUAL:
        case TOKEN_LESS:
        case TOKEN_LESS_EQUAL:
        case TOKEN_ARROW:
        case TOKEN_PIPE:
        case TOKEN_PIPE_PIPE:
        case TOKEN_AMP_AMP:
        case TOKEN_AND:
        case TOKEN_OR:
        case TOKEN_NOT:
        case TOKEN_PLUS_EQUAL:
        case TOKEN_MINUS_EQUAL:
        case TOKEN_STAR_EQUAL:
        case TOKEN_SLASH_EQUAL:
        case TOKEN_DOT:
        case TOKEN_DOT_DOT:
        case TOKEN_NEWLINE:
        case TOKEN_EOF:
            return true;
        default:
            return false;
    }
}

static bool isDigit(char c) {
    return c >= '0' && c <= '9';
}

static bool isAlpha(char c) {
    return (c >= 'a' && c <= 'z') ||
           (c >= 'A' && c <= 'Z') ||
           c == '_';
}

static TokenType checkKeyword(Scanner* scanner, int start, int length,
                              const char* rest, TokenType type) {
    if (scanner->current - scanner->start == start + length &&
        memcmp(scanner->start + start, rest, length) == 0) {
        return type;
    }
    return TOKEN_IDENTIFIER;
}

static TokenType identifierType(Scanner* scanner) {
    switch (scanner->start[0]) {
        case 'a':
            if (scanner->current - scanner->start > 1) {
                switch (scanner->start[1]) {
                    case 'l': return checkKeyword(scanner, 2, 3, "low", TOKEN_ALLOW);
                    case 'n': return checkKeyword(scanner, 2, 1, "d", TOKEN_AND);
                    case 's': return checkKeyword(scanner, 2, 0, "", TOKEN_AS);
                }
            }
            break;
        case 'b': return checkKeyword(scanner, 1, 4, "reak", TOKEN_BREAK);
        case 'c': return checkKeyword(scanner, 1, 7, "ontinue", TOKEN_CONTINUE);
        case 'e':
            if (scanner->current - scanner->start > 1) {
                switch (scanner->start[1]) {
                    case 'l': return checkKeyword(scanner, 2, 2, "se", TOKEN_ELSE);
                    case 'x':
                        if (scanner->current - scanner->start > 2) {
                            switch (scanner->start[2]) {
                                case 'e': return checkKeyword(scanner, 3, 1, "c", TOKEN_EXEC);
                                case 'i': return checkKeyword(scanner, 3, 1, "t", TOKEN_EXIT);
                            }
                        }
                        break;
                }
            }
            break;
        case 'f':
            if (scanner->current - scanner->start > 1) {
                switch (scanner->start[1]) {
                    case 'a':
                        if (scanner->current - scanner->start > 2) {
                            switch (scanner->start[2]) {
                                case 'i': return checkKeyword(scanner, 3, 4, "lure", TOKEN_FAILURE);
                                case 'l': return checkKeyword(scanner, 3, 2, "se", TOKEN_FALSE);
                            }
                        }
                        break;
                    case 'n': return checkKeyword(scanner, 2, 0, "", TOKEN_FN);
                    case 'o': return checkKeyword(scanner, 2, 1, "r", TOKEN_FOR);
                }
            }
            break;
        case 'i':
            if (scanner->current - scanner->start > 1) {
                switch (scanner->start[1]) {
                    case 'f': return checkKeyword(scanner, 2, 0, "", TOKEN_IF);
                    case 'm': return checkKeyword(scanner, 2, 4, "port", TOKEN_IMPORT);
                    case 'n': return checkKeyword(scanner, 2, 0, "", TOKEN_IN);
                }
            }
            break;
        case 'l': return checkKeyword(scanner, 1, 2, "et", TOKEN_LET);
        case 'm':
            if (scanner->current - scanner->start > 1) {
                switch (scanner->start[1]) {
                    case 'a': return checkKeyword(scanner, 2, 3, "tch", TOKEN_MATCH);
                }
            }
            break;
        case 'n':
            if (scanner->current - scanner->start > 1) {
                switch (scanner->start[1]) {
                    case 'i': return checkKeyword(scanner, 2, 1, "l", TOKEN_NIL);
                    case 'o': return checkKeyword(scanner, 2, 1, "t", TOKEN_NOT);
                }
            }
            break;
        case 'o':
            if (scanner->current - scanner->start > 1) {
                switch (scanner->start[1]) {
                    case 'n': return checkKeyword(scanner, 2, 0, "", TOKEN_ON);
                    case 'r': return checkKeyword(scanner, 2, 0, "", TOKEN_OR);
                }
            }
            break;
        case 'p': return checkKeyword(scanner, 1, 7, "arallel", TOKEN_PARALLEL);
        case 'r':
            if (scanner->current - scanner->start > 1) {
                switch (scanner->start[1]) {
                    case 'e':
                        if (scanner->current - scanner->start > 2) {
                            switch (scanner->start[2]) {
                                case 't': return checkKeyword(scanner, 3, 3, "urn", TOKEN_RETURN);
                            }
                        }
                        break;
                }
            }
            break;
        case 't': return checkKeyword(scanner, 1, 3, "rue", TOKEN_TRUE);
        case 'w': return checkKeyword(scanner, 1, 4, "hile", TOKEN_WHILE);
    }
    return TOKEN_IDENTIFIER;
}

static Token scanString(Scanner* scanner, char quote) {
    while (peek(scanner) != quote && !isAtEnd(scanner)) {
        if (peek(scanner) == '\n') {
            scanner->line++;
            scanner->column = 0;
        }
        if (peek(scanner) == '\\' && peekNext(scanner) != '\0') {
            advance(scanner); // skip backslash
        }
        advance(scanner);
    }

    if (isAtEnd(scanner)) return errorToken(scanner, "Unterminated string.");

    advance(scanner); // closing quote
    return makeToken(scanner, TOKEN_STRING);
}

static Token scanRawString(Scanner* scanner) {
    while (peek(scanner) != '`' && !isAtEnd(scanner)) {
        if (peek(scanner) == '\n') {
            scanner->line++;
            scanner->column = 0;
        }
        advance(scanner);
    }

    if (isAtEnd(scanner)) return errorToken(scanner, "Unterminated raw string.");

    advance(scanner); // closing backtick
    return makeToken(scanner, TOKEN_RAW_STRING);
}

static Token scanNumber(Scanner* scanner) {
    while (isDigit(peek(scanner))) advance(scanner);

    // Look for fractional part
    if (peek(scanner) == '.' && isDigit(peekNext(scanner))) {
        advance(scanner); // consume '.'
        while (isDigit(peek(scanner))) advance(scanner);
    }

    // Scientific notation
    if (peek(scanner) == 'e' || peek(scanner) == 'E') {
        advance(scanner);
        if (peek(scanner) == '+' || peek(scanner) == '-') advance(scanner);
        if (!isDigit(peek(scanner))) {
            return errorToken(scanner, "Invalid number: expected digit after exponent.");
        }
        while (isDigit(peek(scanner))) advance(scanner);
    }

    return makeToken(scanner, TOKEN_NUMBER);
}

static Token scanFString(Scanner* scanner, char quote) {
    // scanner->start points to 'f', current is just past the opening quote
    // Scan until closing quote, allowing { } for interpolation
    int braceDepth = 0;
    while (!isAtEnd(scanner)) {
        char ch = peek(scanner);
        if (ch == '\\' && peekNext(scanner) != '\0') {
            advance(scanner); // skip backslash
            advance(scanner); // skip escaped char
            continue;
        }
        if (ch == '{') braceDepth++;
        if (ch == '}') braceDepth--;
        if (ch == quote && braceDepth <= 0) break;
        if (ch == '\n') {
            scanner->line++;
            scanner->column = 0;
        }
        advance(scanner);
    }

    if (isAtEnd(scanner)) return errorToken(scanner, "Unterminated f-string.");
    advance(scanner); // closing quote
    return makeToken(scanner, TOKEN_FSTRING);
}

static Token scanIdentifier(Scanner* scanner) {
    while (isAlpha(peek(scanner)) || isDigit(peek(scanner))) advance(scanner);

    // Check for f-string: identifier is exactly "f" followed by quote
    if ((scanner->current - scanner->start == 1) &&
        *scanner->start == 'f' &&
        (peek(scanner) == '"' || peek(scanner) == '\'')) {
        char quote = advance(scanner); // consume opening quote
        return scanFString(scanner, quote);
    }

    return makeToken(scanner, identifierType(scanner));
}

Token scanToken(Scanner* scanner) {
    skipWhitespace(scanner);

    scanner->start = scanner->current;
    scanner->startColumn = scanner->column;

    if (isAtEnd(scanner)) return makeToken(scanner, TOKEN_EOF);

    char c = advance(scanner);

    // Newline handling
    if (c == '\n') {
        scanner->line++;
        scanner->column = 1;
        // Suppress newline if previous token expects continuation
        if (suppressNewlineAfter(scanner->previous)) {
            return scanToken(scanner); // skip this newline, get next real token
        }
        return makeToken(scanner, TOKEN_NEWLINE);
    }

    if (isAlpha(c)) return scanIdentifier(scanner);
    if (isDigit(c)) return scanNumber(scanner);

    switch (c) {
        case '(': return makeToken(scanner, TOKEN_LEFT_PAREN);
        case ')': return makeToken(scanner, TOKEN_RIGHT_PAREN);
        case '{': return makeToken(scanner, TOKEN_LEFT_BRACE);
        case '}': return makeToken(scanner, TOKEN_RIGHT_BRACE);
        case '[': return makeToken(scanner, TOKEN_LEFT_BRACKET);
        case ']': return makeToken(scanner, TOKEN_RIGHT_BRACKET);
        case ',': return makeToken(scanner, TOKEN_COMMA);
        case ':': return makeToken(scanner, TOKEN_COLON);
        case ';': return makeToken(scanner, TOKEN_SEMICOLON);
        case '+':
            if (match(scanner, '=')) return makeToken(scanner, TOKEN_PLUS_EQUAL);
            return makeToken(scanner, TOKEN_PLUS);
        case '-':
            if (match(scanner, '>')) return makeToken(scanner, TOKEN_ARROW);
            if (match(scanner, '=')) return makeToken(scanner, TOKEN_MINUS_EQUAL);
            return makeToken(scanner, TOKEN_MINUS);
        case '*':
            if (match(scanner, '=')) return makeToken(scanner, TOKEN_STAR_EQUAL);
            return makeToken(scanner, TOKEN_STAR);
        case '/':
            if (match(scanner, '=')) return makeToken(scanner, TOKEN_SLASH_EQUAL);
            return makeToken(scanner, TOKEN_SLASH);
        case '%': return makeToken(scanner, TOKEN_PERCENT);
        case '!':
            return makeToken(scanner, match(scanner, '=') ? TOKEN_BANG_EQUAL : TOKEN_BANG);
        case '=':
            return makeToken(scanner, match(scanner, '=') ? TOKEN_EQUAL_EQUAL : TOKEN_EQUAL);
        case '<':
            return makeToken(scanner, match(scanner, '=') ? TOKEN_LESS_EQUAL : TOKEN_LESS);
        case '>':
            return makeToken(scanner, match(scanner, '=') ? TOKEN_GREATER_EQUAL : TOKEN_GREATER);
        case '|':
            return makeToken(scanner, match(scanner, '|') ? TOKEN_PIPE_PIPE : TOKEN_PIPE);
        case '&':
            return makeToken(scanner, match(scanner, '&') ? TOKEN_AMP_AMP : TOKEN_AMP);
        case '.':
            if (match(scanner, '.')) return makeToken(scanner, TOKEN_DOT_DOT);
            return makeToken(scanner, TOKEN_DOT);
        case '"': return scanString(scanner, '"');
        case '\'': return scanString(scanner, '\'');
        case '`': return scanRawString(scanner);
    }

    return errorToken(scanner, "Unexpected character.");
}

const char* tokenTypeName(TokenType type) {
    switch (type) {
        case TOKEN_LEFT_PAREN: return "LEFT_PAREN";
        case TOKEN_RIGHT_PAREN: return "RIGHT_PAREN";
        case TOKEN_LEFT_BRACE: return "LEFT_BRACE";
        case TOKEN_RIGHT_BRACE: return "RIGHT_BRACE";
        case TOKEN_LEFT_BRACKET: return "LEFT_BRACKET";
        case TOKEN_RIGHT_BRACKET: return "RIGHT_BRACKET";
        case TOKEN_COMMA: return "COMMA";
        case TOKEN_DOT: return "DOT";
        case TOKEN_COLON: return "COLON";
        case TOKEN_SEMICOLON: return "SEMICOLON";
        case TOKEN_PLUS: return "PLUS";
        case TOKEN_MINUS: return "MINUS";
        case TOKEN_STAR: return "STAR";
        case TOKEN_SLASH: return "SLASH";
        case TOKEN_PERCENT: return "PERCENT";
        case TOKEN_BANG: return "BANG";
        case TOKEN_BANG_EQUAL: return "BANG_EQUAL";
        case TOKEN_EQUAL: return "EQUAL";
        case TOKEN_EQUAL_EQUAL: return "EQUAL_EQUAL";
        case TOKEN_GREATER: return "GREATER";
        case TOKEN_GREATER_EQUAL: return "GREATER_EQUAL";
        case TOKEN_LESS: return "LESS";
        case TOKEN_LESS_EQUAL: return "LESS_EQUAL";
        case TOKEN_ARROW: return "ARROW";
        case TOKEN_PIPE: return "PIPE";
        case TOKEN_PIPE_PIPE: return "PIPE_PIPE";
        case TOKEN_AMP: return "AMP";
        case TOKEN_AMP_AMP: return "AMP_AMP";
        case TOKEN_PLUS_EQUAL: return "PLUS_EQUAL";
        case TOKEN_MINUS_EQUAL: return "MINUS_EQUAL";
        case TOKEN_STAR_EQUAL: return "STAR_EQUAL";
        case TOKEN_SLASH_EQUAL: return "SLASH_EQUAL";
        case TOKEN_DOT_DOT: return "DOT_DOT";
        case TOKEN_IDENTIFIER: return "IDENTIFIER";
        case TOKEN_STRING: return "STRING";
        case TOKEN_NUMBER: return "NUMBER";
        case TOKEN_RAW_STRING: return "RAW_STRING";
        case TOKEN_FSTRING: return "FSTRING";
        case TOKEN_ALLOW: return "ALLOW";
        case TOKEN_AND: return "AND";
        case TOKEN_AS: return "AS";
        case TOKEN_BREAK: return "BREAK";
        case TOKEN_CONTINUE: return "CONTINUE";
        case TOKEN_ELSE: return "ELSE";
        case TOKEN_EXEC: return "EXEC";
        case TOKEN_EXIT: return "EXIT";
        case TOKEN_FAILURE: return "FAILURE";
        case TOKEN_FALSE: return "FALSE";
        case TOKEN_FN: return "FN";
        case TOKEN_FOR: return "FOR";
        case TOKEN_IF: return "IF";
        case TOKEN_IMPORT: return "IMPORT";
        case TOKEN_IN: return "IN";
        case TOKEN_LET: return "LET";
        case TOKEN_MATCH: return "MATCH";
        case TOKEN_NET: return "NET";
        case TOKEN_NIL: return "NIL";
        case TOKEN_NOT: return "NOT";
        case TOKEN_ON: return "ON";
        case TOKEN_OR: return "OR";
        case TOKEN_PARALLEL: return "PARALLEL";
        case TOKEN_READ: return "READ";
        case TOKEN_RETURN: return "RETURN";
        case TOKEN_TRUE: return "TRUE";
        case TOKEN_WHILE: return "WHILE";
        case TOKEN_WRITE: return "WRITE";
        case TOKEN_ENV: return "ENV";
        case TOKEN_NEWLINE: return "NEWLINE";
        case TOKEN_ERROR: return "ERROR";
        case TOKEN_EOF: return "EOF";
    }
    return "UNKNOWN";
}
