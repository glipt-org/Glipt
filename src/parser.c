#include "parser.h"
#include "common.h"

// ---- Dynamic array helper for collecting nodes during parsing ----

typedef struct {
    AstNode** items;
    int count;
    int capacity;
    Arena* arena;
} NodeList;

static void nodeListInit(NodeList* list, Arena* arena) {
    list->items = NULL;
    list->count = 0;
    list->capacity = 0;
    list->arena = arena;
}

static void nodeListAdd(NodeList* list, AstNode* node) {
    if (list->count >= list->capacity) {
        int oldCapacity = list->capacity;
        list->capacity = oldCapacity < 8 ? 8 : oldCapacity * 2;
        // Allocate new array from arena (old one is not freed - arena handles it)
        AstNode** newItems = (AstNode**)arenaAlloc(list->arena, sizeof(AstNode*) * list->capacity);
        if (oldCapacity > 0) {
            memcpy(newItems, list->items, sizeof(AstNode*) * oldCapacity);
        }
        list->items = newItems;
    }
    list->items[list->count++] = node;
}

// Finalize: return a tight copy of the items array
static AstNode** nodeListFinalize(NodeList* list, Arena* arena) {
    if (list->count == 0) return NULL;
    AstNode** result = (AstNode**)arenaAlloc(arena, sizeof(AstNode*) * list->count);
    memcpy(result, list->items, sizeof(AstNode*) * list->count);
    return result;
}

// ---- Error Handling ----

static void errorAt(Parser* parser, Token* token, const char* message) {
    if (parser->panicMode) return;
    parser->panicMode = true;
    parser->hadError = true;

    fprintf(stderr, "[line %d, col %d] Error", token->line, token->column);

    if (token->type == TOKEN_EOF) {
        fprintf(stderr, " at end");
    } else if (token->type == TOKEN_ERROR) {
        // nothing
    } else {
        fprintf(stderr, " at '%.*s'", token->length, token->start);
    }

    fprintf(stderr, ": %s\n", message);
}

static void errorAtCurrent(Parser* parser, const char* message) {
    errorAt(parser, &parser->current, message);
}

// ---- Token Management ----

static void advanceParser(Parser* parser) {
    parser->previous = parser->current;

    for (;;) {
        parser->current = scanToken(&parser->scanner);
        if (parser->current.type != TOKEN_ERROR) break;
        errorAtCurrent(parser, parser->current.start);
    }
}

static void consume(Parser* parser, TokenType type, const char* message) {
    if (parser->current.type == type) {
        advanceParser(parser);
        return;
    }
    errorAtCurrent(parser, message);
}

static bool check(Parser* parser, TokenType type) {
    return parser->current.type == type;
}

static bool isKeywordToken(TokenType type) {
    return type >= TOKEN_ALLOW && type <= TOKEN_ENV;
}

// After '.', accept identifiers and keywords as property names
static void consumePropertyName(Parser* parser) {
    if (parser->current.type == TOKEN_IDENTIFIER || isKeywordToken(parser->current.type)) {
        advanceParser(parser);
        return;
    }
    errorAtCurrent(parser, "Expected property name after '.'.");
}

static bool matchToken(Parser* parser, TokenType type) {
    if (!check(parser, type)) return false;
    advanceParser(parser);
    return true;
}

// Skip optional newlines
static void skipNewlines(Parser* parser) {
    while (check(parser, TOKEN_NEWLINE)) {
        advanceParser(parser);
    }
}

// Expect a statement terminator (newline, EOF, or '}' peeked)
static void expectTerminator(Parser* parser) {
    if (check(parser, TOKEN_NEWLINE)) {
        advanceParser(parser);
        return;
    }
    if (check(parser, TOKEN_EOF) || check(parser, TOKEN_RIGHT_BRACE)) {
        return;
    }
    errorAtCurrent(parser, "Expected newline or end of statement.");
}

// ---- Synchronize after error ----

static void synchronize(Parser* parser) {
    parser->panicMode = false;

    while (parser->current.type != TOKEN_EOF) {
        if (parser->previous.type == TOKEN_NEWLINE) return;

        switch (parser->current.type) {
            case TOKEN_FN:
            case TOKEN_LET:
            case TOKEN_IF:
            case TOKEN_WHILE:
            case TOKEN_FOR:
            case TOKEN_RETURN:
            case TOKEN_ALLOW:
            case TOKEN_ON:
            case TOKEN_PARALLEL:
            case TOKEN_IMPORT:
            case TOKEN_EXEC:
            case TOKEN_MATCH:
                return;
            default:
                break;
        }

        advanceParser(parser);
    }
}

// ---- Forward Declarations ----

static AstNode* parseExpression(Parser* parser);
static AstNode* parseDeclaration(Parser* parser);
static AstNode* parseStatement(Parser* parser);
static AstNode* parseBlock(Parser* parser);
static AstNode* parseMatch(Parser* parser);

// ---- Expression Parsing (Pratt-style precedence climbing) ----

// Precedence levels (low to high)
typedef enum {
    PREC_NONE,
    PREC_ASSIGNMENT,    // =
    PREC_PIPE,          // |
    PREC_OR,            // or ||
    PREC_AND,           // and &&
    PREC_EQUALITY,      // == !=
    PREC_COMPARISON,    // < > <= >=
    PREC_RANGE,         // ..
    PREC_ADDITION,      // + -
    PREC_MULTIPLICATION,// * / %
    PREC_UNARY,         // - ! not
    PREC_CALL,          // . () []
    PREC_PRIMARY
} Precedence;

// Get precedence of a binary operator token
static Precedence getBinaryPrecedence(TokenType type) {
    switch (type) {
        case TOKEN_PIPE:         return PREC_PIPE;
        case TOKEN_OR:
        case TOKEN_PIPE_PIPE:    return PREC_OR;
        case TOKEN_AND:
        case TOKEN_AMP_AMP:      return PREC_AND;
        case TOKEN_EQUAL_EQUAL:
        case TOKEN_BANG_EQUAL:   return PREC_EQUALITY;
        case TOKEN_LESS:
        case TOKEN_LESS_EQUAL:
        case TOKEN_GREATER:
        case TOKEN_GREATER_EQUAL: return PREC_COMPARISON;
        case TOKEN_DOT_DOT:     return PREC_RANGE;
        case TOKEN_PLUS:
        case TOKEN_MINUS:        return PREC_ADDITION;
        case TOKEN_STAR:
        case TOKEN_SLASH:
        case TOKEN_PERCENT:      return PREC_MULTIPLICATION;
        default:                 return PREC_NONE;
    }
}

static AstNode* parsePrimary(Parser* parser);
static AstNode* parsePrecedence(Parser* parser, Precedence minPrec);

static AstNode* parseNumber(Parser* parser) {
    double value = strtod(parser->previous.start, NULL);
    return astNewLiteralNumber(parser->arena, value,
                               parser->previous.line, parser->previous.column);
}

static AstNode* parseString(Parser* parser) {
    // Strip quotes: the token includes the surrounding quotes
    const char* chars = parser->previous.start + 1;
    int length = parser->previous.length - 2;
    bool isRaw = parser->previous.type == TOKEN_RAW_STRING;
    return astNewLiteralString(parser->arena, chars, length, isRaw,
                                parser->previous.line, parser->previous.column);
}

// Helper: wrap an expression in str(expr)
static AstNode* wrapInStr(Parser* parser, AstNode* expr, int line, int col) {
    AstNode* strVar = astNewVariable(parser->arena, "str", 3, line, col);
    AstNode** args = (AstNode**)arenaAlloc(parser->arena, sizeof(AstNode*));
    args[0] = expr;
    return astNewCall(parser->arena, strVar, args, 1, line, col);
}

static AstNode* parseFString(Parser* parser) {
    Token token = parser->previous;
    int line = token.line;
    int col = token.column;

    // Token is f"content" or f'content' — strip f, quote, and closing quote
    const char* raw = token.start + 2;  // skip 'f' and opening quote
    int rawLen = token.length - 3;       // subtract f, open quote, close quote

    AstNode* result = NULL;

    int i = 0;
    while (i < rawLen) {
        // Find next '{' or end
        int litStart = i;
        while (i < rawLen && raw[i] != '{') {
            if (raw[i] == '\\' && i + 1 < rawLen) {
                i += 2; // skip escaped char
            } else {
                i++;
            }
        }

        // Emit literal part if non-empty
        if (i > litStart) {
            AstNode* lit = astNewLiteralString(parser->arena,
                raw + litStart, i - litStart, false, line, col);
            if (result == NULL) {
                result = lit;
            } else {
                result = astNewBinary(parser->arena, TOKEN_PLUS,
                    result, lit, line, col);
            }
        }

        if (i >= rawLen) break;

        // Skip '{'
        i++;

        // Find matching '}', handling nested braces
        int exprStart = i;
        int depth = 1;
        while (i < rawLen && depth > 0) {
            if (raw[i] == '{') depth++;
            else if (raw[i] == '}') depth--;
            if (depth > 0) i++;
        }

        if (depth != 0) {
            errorAt(parser, &token, "Unterminated interpolation in f-string.");
            return result ? result : astNewLiteralString(
                parser->arena, "", 0, false, line, col);
        }

        int exprLen = i - exprStart;
        i++; // skip '}'

        if (exprLen == 0) continue;

        // Parse the expression: create a null-terminated copy, set up temp scanner
        char* exprText = (char*)arenaAlloc(parser->arena, exprLen + 1);
        memcpy(exprText, raw + exprStart, exprLen);
        exprText[exprLen] = '\0';

        // Save parser state
        Scanner savedScanner = parser->scanner;
        Token savedCurrent = parser->current;
        Token savedPrevious = parser->previous;
        bool savedHadError = parser->hadError;
        bool savedPanicMode = parser->panicMode;

        // Parse expression with temp scanner
        initScanner(&parser->scanner, exprText);
        advanceParser(parser); // prime with first token
        AstNode* expr = parseExpression(parser);

        // Restore parser state
        parser->scanner = savedScanner;
        parser->current = savedCurrent;
        parser->previous = savedPrevious;
        parser->hadError = savedHadError;
        parser->panicMode = savedPanicMode;

        if (expr == NULL) continue;

        // Wrap in str() for non-string expressions
        AstNode* part = wrapInStr(parser, expr, line, col);

        if (result == NULL) {
            result = part;
        } else {
            result = astNewBinary(parser->arena, TOKEN_PLUS,
                result, part, line, col);
        }
    }

    // If the whole f-string was empty
    if (result == NULL) {
        result = astNewLiteralString(parser->arena, "", 0, false, line, col);
    }

    return result;
}

static AstNode* parseGrouping(Parser* parser) {
    AstNode* expr = parseExpression(parser);
    consume(parser, TOKEN_RIGHT_PAREN, "Expected ')' after expression.");
    return expr;
}

static AstNode* parseList(Parser* parser) {
    int line = parser->previous.line;
    int col = parser->previous.column;
    NodeList elements;
    nodeListInit(&elements, parser->arena);

    skipNewlines(parser);
    if (!check(parser, TOKEN_RIGHT_BRACKET)) {
        do {
            skipNewlines(parser);
            if (check(parser, TOKEN_RIGHT_BRACKET)) break; // trailing comma
            nodeListAdd(&elements, parseExpression(parser));
            skipNewlines(parser);
        } while (matchToken(parser, TOKEN_COMMA));
    }
    skipNewlines(parser);
    consume(parser, TOKEN_RIGHT_BRACKET, "Expected ']' after list elements.");

    return astNewList(parser->arena, nodeListFinalize(&elements, parser->arena),
                      elements.count, line, col);
}

static AstNode* parseMap(Parser* parser) {
    int line = parser->previous.line;
    int col = parser->previous.column;
    NodeList keys, values;
    nodeListInit(&keys, parser->arena);
    nodeListInit(&values, parser->arena);

    skipNewlines(parser);
    if (!check(parser, TOKEN_RIGHT_BRACE)) {
        do {
            skipNewlines(parser);
            if (check(parser, TOKEN_RIGHT_BRACE)) break; // trailing comma

            // Key: string or identifier
            AstNode* key;
            if (matchToken(parser, TOKEN_STRING)) {
                key = parseString(parser);
            } else if (matchToken(parser, TOKEN_IDENTIFIER)) {
                // Treat bare identifier as a string key
                key = astNewLiteralString(parser->arena,
                    parser->previous.start, parser->previous.length, false,
                    parser->previous.line, parser->previous.column);
            } else {
                errorAtCurrent(parser, "Expected string or identifier as map key.");
                return NULL;
            }
            nodeListAdd(&keys, key);

            consume(parser, TOKEN_COLON, "Expected ':' after map key.");
            skipNewlines(parser);
            nodeListAdd(&values, parseExpression(parser));
            skipNewlines(parser);
        } while (matchToken(parser, TOKEN_COMMA));
    }
    skipNewlines(parser);
    consume(parser, TOKEN_RIGHT_BRACE, "Expected '}' after map entries.");

    return astNewMap(parser->arena,
                     nodeListFinalize(&keys, parser->arena),
                     nodeListFinalize(&values, parser->arena),
                     keys.count, line, col);
}

// Helper to parse function parameters (used by both fn decl and lambda)
typedef struct {
    const char** names;
    int* lengths;
    int count;
} ParamList;

static ParamList parseFnParams(Parser* parser) {
    ParamList params;
    params.count = 0;

    // Temporary storage (max 255 params)
    const char* tempNames[255];
    int tempLengths[255];

    if (!check(parser, TOKEN_RIGHT_PAREN)) {
        do {
            if (params.count >= 255) {
                errorAtCurrent(parser, "Can't have more than 255 parameters.");
                break;
            }
            consume(parser, TOKEN_IDENTIFIER, "Expected parameter name.");
            tempNames[params.count] = parser->previous.start;
            tempLengths[params.count] = parser->previous.length;
            params.count++;
        } while (matchToken(parser, TOKEN_COMMA));
    }
    consume(parser, TOKEN_RIGHT_PAREN, "Expected ')' after parameters.");

    if (params.count > 0) {
        params.names = (const char**)arenaAlloc(parser->arena, sizeof(const char*) * params.count);
        params.lengths = (int*)arenaAlloc(parser->arena, sizeof(int) * params.count);
        memcpy(params.names, tempNames, sizeof(const char*) * params.count);
        memcpy(params.lengths, tempLengths, sizeof(int) * params.count);
    } else {
        params.names = NULL;
        params.lengths = NULL;
    }

    return params;
}

static AstNode* parsePrimary(Parser* parser) {
    // Number
    if (matchToken(parser, TOKEN_NUMBER)) {
        return parseNumber(parser);
    }

    // String
    if (matchToken(parser, TOKEN_STRING) || matchToken(parser, TOKEN_RAW_STRING)) {
        return parseString(parser);
    }

    // F-string (interpolated)
    if (matchToken(parser, TOKEN_FSTRING)) {
        return parseFString(parser);
    }

    // Boolean
    if (matchToken(parser, TOKEN_TRUE)) {
        return astNewLiteralBool(parser->arena, true,
                                  parser->previous.line, parser->previous.column);
    }
    if (matchToken(parser, TOKEN_FALSE)) {
        return astNewLiteralBool(parser->arena, false,
                                  parser->previous.line, parser->previous.column);
    }

    // Nil
    if (matchToken(parser, TOKEN_NIL)) {
        return astNewLiteralNil(parser->arena,
                                 parser->previous.line, parser->previous.column);
    }

    // Identifier
    if (matchToken(parser, TOKEN_IDENTIFIER)) {
        return astNewVariable(parser->arena,
                               parser->previous.start, parser->previous.length,
                               parser->previous.line, parser->previous.column);
    }

    // Grouping: (expr)
    if (matchToken(parser, TOKEN_LEFT_PAREN)) {
        return parseGrouping(parser);
    }

    // List literal: [...]
    if (matchToken(parser, TOKEN_LEFT_BRACKET)) {
        return parseList(parser);
    }

    // Map literal: {...}
    if (matchToken(parser, TOKEN_LEFT_BRACE)) {
        return parseMap(parser);
    }

    // Lambda: fn(params) { body }
    if (matchToken(parser, TOKEN_FN)) {
        int line = parser->previous.line;
        int col = parser->previous.column;

        consume(parser, TOKEN_LEFT_PAREN, "Expected '(' after 'fn' in lambda.");
        ParamList params = parseFnParams(parser);
        skipNewlines(parser);
        AstNode* body = parseBlock(parser);

        return astNewLambda(parser->arena, params.names, params.lengths,
                            params.count, body, line, col);
    }

    // Exec expression: exec "command" or `command`
    if (matchToken(parser, TOKEN_EXEC)) {
        int line = parser->previous.line;
        int col = parser->previous.column;
        AstNode* command = parseExpression(parser);
        return astNewExec(parser->arena, command, NULL, 0, line, col);
    }

    // Match expression: match expr { pattern -> body, ... }
    if (matchToken(parser, TOKEN_MATCH)) {
        return parseMatch(parser);
    }

    errorAtCurrent(parser, "Expected expression.");
    return astNewLiteralNil(parser->arena,
                             parser->current.line, parser->current.column);
}

// Parse postfix operations: calls, indexing, dot access
static AstNode* parsePostfix(Parser* parser, AstNode* left) {
    for (;;) {
        // Function call: expr(args)
        if (matchToken(parser, TOKEN_LEFT_PAREN)) {
            int line = parser->previous.line;
            int col = parser->previous.column;
            NodeList args;
            nodeListInit(&args, parser->arena);

            skipNewlines(parser);
            if (!check(parser, TOKEN_RIGHT_PAREN)) {
                do {
                    skipNewlines(parser);
                    nodeListAdd(&args, parseExpression(parser));
                    skipNewlines(parser);
                } while (matchToken(parser, TOKEN_COMMA));
            }
            skipNewlines(parser);
            consume(parser, TOKEN_RIGHT_PAREN, "Expected ')' after arguments.");

            left = astNewCall(parser->arena, left,
                              nodeListFinalize(&args, parser->arena),
                              args.count, line, col);
            continue;
        }

        // Index: expr[index]
        if (matchToken(parser, TOKEN_LEFT_BRACKET)) {
            int line = parser->previous.line;
            int col = parser->previous.column;
            AstNode* index = parseExpression(parser);
            consume(parser, TOKEN_RIGHT_BRACKET, "Expected ']' after index.");

            // Check for assignment: expr[index] = value
            if (matchToken(parser, TOKEN_EQUAL)) {
                AstNode* value = parseExpression(parser);
                left = astNewIndexSet(parser->arena, left, index, value, line, col);
            } else {
                left = astNewIndex(parser->arena, left, index, line, col);
            }
            continue;
        }

        // Dot access: expr.name
        if (matchToken(parser, TOKEN_DOT)) {
            int line = parser->previous.line;
            int col = parser->previous.column;
            consumePropertyName(parser);
            const char* name = parser->previous.start;
            int nameLen = parser->previous.length;

            // Check for assignment: expr.name = value
            if (matchToken(parser, TOKEN_EQUAL)) {
                AstNode* value = parseExpression(parser);
                left = astNewDotSet(parser->arena, left, name, nameLen, value, line, col);
            } else {
                left = astNewDot(parser->arena, left, name, nameLen, line, col);
            }
            continue;
        }

        break;
    }
    return left;
}

// Pratt parser: parse expressions with precedence climbing
static AstNode* parsePrecedence(Parser* parser, Precedence minPrec) {
    AstNode* left;

    // Handle prefix operators
    if (matchToken(parser, TOKEN_MINUS)) {
        int line = parser->previous.line;
        int col = parser->previous.column;
        AstNode* operand = parsePrecedence(parser, PREC_UNARY);
        left = astNewUnary(parser->arena, TOKEN_MINUS, operand, line, col);
    } else if (matchToken(parser, TOKEN_BANG) || matchToken(parser, TOKEN_NOT)) {
        int line = parser->previous.line;
        int col = parser->previous.column;
        TokenType op = parser->previous.type;
        AstNode* operand = parsePrecedence(parser, PREC_UNARY);
        left = astNewUnary(parser->arena, op, operand, line, col);
    } else {
        left = parsePrimary(parser);
    }

    // Postfix
    left = parsePostfix(parser, left);

    // Infix binary operators (precedence climbing)
    for (;;) {
        TokenType opType = parser->current.type;
        Precedence prec = getBinaryPrecedence(opType);

        if (prec == PREC_NONE || prec < minPrec) break;

        advanceParser(parser);
        int line = parser->previous.line;
        int col = parser->previous.column;

        if (opType == TOKEN_PIPE) {
            // Pipe: left | right (right-associative conceptually, but left-to-right chain)
            AstNode* right = parsePrecedence(parser, prec + 1);
            right = parsePostfix(parser, right);
            left = astNewPipe(parser->arena, left, right, line, col);
        } else if (opType == TOKEN_DOT_DOT) {
            // Range: left..right
            AstNode* right = parsePrecedence(parser, prec + 1);
            right = parsePostfix(parser, right);
            left = astNewRange(parser->arena, left, right, line, col);
        } else {
            // Standard binary
            AstNode* right = parsePrecedence(parser, prec + 1);
            right = parsePostfix(parser, right);
            left = astNewBinary(parser->arena, opType, left, right, line, col);
        }
    }

    return left;
}

static AstNode* parseExpression(Parser* parser) {
    return parsePrecedence(parser, PREC_PIPE);
}

// ---- Statement Parsing ----

static AstNode* parseBlock(Parser* parser) {
    int line = parser->current.line;
    int col = parser->current.column;

    consume(parser, TOKEN_LEFT_BRACE, "Expected '{' to begin block.");
    skipNewlines(parser);

    NodeList stmts;
    nodeListInit(&stmts, parser->arena);

    while (!check(parser, TOKEN_RIGHT_BRACE) && !check(parser, TOKEN_EOF)) {
        AstNode* decl = parseDeclaration(parser);
        if (decl != NULL) {
            nodeListAdd(&stmts, decl);
        }
        skipNewlines(parser);
    }

    consume(parser, TOKEN_RIGHT_BRACE, "Expected '}' after block.");

    return astNewBlock(parser->arena, nodeListFinalize(&stmts, parser->arena),
                       stmts.count, line, col);
}

static AstNode* parseIfStatement(Parser* parser) {
    int line = parser->previous.line;
    int col = parser->previous.column;

    AstNode* condition = parseExpression(parser);
    skipNewlines(parser);
    AstNode* thenBranch = parseBlock(parser);

    AstNode* elseBranch = NULL;
    skipNewlines(parser);
    if (matchToken(parser, TOKEN_ELSE)) {
        skipNewlines(parser);
        if (matchToken(parser, TOKEN_IF)) {
            elseBranch = parseIfStatement(parser);
        } else {
            elseBranch = parseBlock(parser);
        }
    }

    return astNewIf(parser->arena, condition, thenBranch, elseBranch, line, col);
}

static AstNode* parseWhileStatement(Parser* parser) {
    int line = parser->previous.line;
    int col = parser->previous.column;

    AstNode* condition = parseExpression(parser);
    skipNewlines(parser);
    AstNode* body = parseBlock(parser);

    return astNewWhile(parser->arena, condition, body, line, col);
}

static AstNode* parseForStatement(Parser* parser) {
    int line = parser->previous.line;
    int col = parser->previous.column;

    consume(parser, TOKEN_IDENTIFIER, "Expected variable name after 'for'.");
    const char* varName = parser->previous.start;
    int varNameLen = parser->previous.length;

    consume(parser, TOKEN_IN, "Expected 'in' after for variable.");
    AstNode* iterable = parseExpression(parser);
    skipNewlines(parser);
    AstNode* body = parseBlock(parser);

    return astNewFor(parser->arena, varName, varNameLen, iterable, body, line, col);
}

static AstNode* parseReturnStatement(Parser* parser) {
    int line = parser->previous.line;
    int col = parser->previous.column;

    AstNode* value = NULL;
    if (!check(parser, TOKEN_NEWLINE) && !check(parser, TOKEN_EOF) &&
        !check(parser, TOKEN_RIGHT_BRACE)) {
        value = parseExpression(parser);
    }
    expectTerminator(parser);
    return astNewReturn(parser->arena, value, line, col);
}

static AstNode* parseAllowDeclaration(Parser* parser) {
    int line = parser->previous.line;
    int col = parser->previous.column;

    // Expect permission type: exec is a keyword, others are identifiers
    TokenType permType;
    if (matchToken(parser, TOKEN_EXEC)) {
        permType = TOKEN_EXEC;
    } else if (matchToken(parser, TOKEN_IDENTIFIER)) {
        // read, write, net, env are identifiers but used as perm types after 'allow'
        if (parser->previous.length == 3 && memcmp(parser->previous.start, "net", 3) == 0) {
            permType = TOKEN_NET;
        } else if (parser->previous.length == 4 && memcmp(parser->previous.start, "read", 4) == 0) {
            permType = TOKEN_READ;
        } else if (parser->previous.length == 5 && memcmp(parser->previous.start, "write", 5) == 0) {
            permType = TOKEN_WRITE;
        } else if (parser->previous.length == 3 && memcmp(parser->previous.start, "env", 3) == 0) {
            permType = TOKEN_ENV;
        } else {
            errorAtCurrent(parser, "Expected permission type (exec, net, read, write, env) after 'allow'.");
            return NULL;
        }
    } else {
        errorAtCurrent(parser, "Expected permission type (exec, net, read, write, env) after 'allow'.");
        return NULL;
    }

    // Expect target string
    consume(parser, TOKEN_STRING, "Expected string after permission type.");
    const char* target = parser->previous.start + 1;  // skip quote
    int targetLen = parser->previous.length - 2;       // strip both quotes
    expectTerminator(parser);

    return astNewAllow(parser->arena, permType, target, targetLen, line, col);
}

static AstNode* parseFnDeclaration(Parser* parser) {
    int line = parser->previous.line;
    int col = parser->previous.column;

    consume(parser, TOKEN_IDENTIFIER, "Expected function name after 'fn'.");
    const char* name = parser->previous.start;
    int nameLen = parser->previous.length;

    consume(parser, TOKEN_LEFT_PAREN, "Expected '(' after function name.");
    ParamList params = parseFnParams(parser);
    skipNewlines(parser);
    AstNode* body = parseBlock(parser);

    return astNewFnDecl(parser->arena, name, nameLen, params.names, params.lengths,
                        params.count, body, line, col);
}

static AstNode* parseParallelBlock(Parser* parser) {
    int line = parser->previous.line;
    int col = parser->previous.column;

    consume(parser, TOKEN_LEFT_BRACE, "Expected '{' after 'parallel'.");
    skipNewlines(parser);

    NodeList tasks;
    nodeListInit(&tasks, parser->arena);

    while (!check(parser, TOKEN_RIGHT_BRACE) && !check(parser, TOKEN_EOF)) {
        AstNode* task = parseStatement(parser);
        if (task != NULL) {
            nodeListAdd(&tasks, task);
        }
        skipNewlines(parser);
    }

    consume(parser, TOKEN_RIGHT_BRACE, "Expected '}' after parallel block.");

    return astNewParallel(parser->arena, nodeListFinalize(&tasks, parser->arena),
                          tasks.count, line, col);
}

static AstNode* parseOnFailure(Parser* parser) {
    int line = parser->previous.line;
    int col = parser->previous.column;

    consume(parser, TOKEN_FAILURE, "Expected 'failure' after 'on'.");
    skipNewlines(parser);
    AstNode* body = parseBlock(parser);

    return astNewOnFailure(parser->arena, body, line, col);
}

static AstNode* parseImport(Parser* parser) {
    int line = parser->previous.line;
    int col = parser->previous.column;

    consume(parser, TOKEN_STRING, "Expected string after 'import'.");
    const char* path = parser->previous.start + 1;
    int pathLen = parser->previous.length - 2;

    const char* alias = NULL;
    int aliasLen = 0;

    if (matchToken(parser, TOKEN_AS)) {
        consume(parser, TOKEN_IDENTIFIER, "Expected identifier after 'as'.");
        alias = parser->previous.start;
        aliasLen = parser->previous.length;
    }
    expectTerminator(parser);

    return astNewImport(parser->arena, path, pathLen, alias, aliasLen, line, col);
}

static AstNode* parseMatch(Parser* parser) {
    int line = parser->previous.line;
    int col = parser->previous.column;

    AstNode* subject = parseExpression(parser);
    skipNewlines(parser);
    consume(parser, TOKEN_LEFT_BRACE, "Expected '{' after match expression.");
    skipNewlines(parser);

    NodeList arms;
    nodeListInit(&arms, parser->arena);

    while (!check(parser, TOKEN_RIGHT_BRACE) && !check(parser, TOKEN_EOF)) {
        int armLine = parser->current.line;
        int armCol = parser->current.column;

        // Pattern (or _ for wildcard)
        AstNode* pattern;
        if (matchToken(parser, TOKEN_IDENTIFIER) &&
            parser->previous.length == 1 && parser->previous.start[0] == '_') {
            pattern = NULL; // wildcard
        } else {
            pattern = parseExpression(parser);
        }

        consume(parser, TOKEN_ARROW, "Expected '->' after match pattern.");
        skipNewlines(parser);

        AstNode* body;
        if (check(parser, TOKEN_LEFT_BRACE)) {
            body = parseBlock(parser);
        } else {
            body = parseExpression(parser);
        }

        nodeListAdd(&arms, astNewMatchArm(parser->arena, pattern, body, armLine, armCol));
        skipNewlines(parser);
    }

    consume(parser, TOKEN_RIGHT_BRACE, "Expected '}' after match arms.");

    return astNewMatch(parser->arena, subject,
                       nodeListFinalize(&arms, parser->arena),
                       arms.count, line, col);
}

static AstNode* parseStatement(Parser* parser) {
    if (matchToken(parser, TOKEN_IF)) {
        return parseIfStatement(parser);
    }

    if (matchToken(parser, TOKEN_WHILE)) {
        return parseWhileStatement(parser);
    }

    if (matchToken(parser, TOKEN_FOR)) {
        return parseForStatement(parser);
    }

    if (matchToken(parser, TOKEN_RETURN)) {
        return parseReturnStatement(parser);
    }

    if (matchToken(parser, TOKEN_BREAK)) {
        int line = parser->previous.line;
        int col = parser->previous.column;
        expectTerminator(parser);
        return astNewBreak(parser->arena, line, col);
    }

    if (matchToken(parser, TOKEN_CONTINUE)) {
        int line = parser->previous.line;
        int col = parser->previous.column;
        expectTerminator(parser);
        return astNewContinue(parser->arena, line, col);
    }

    if (matchToken(parser, TOKEN_PARALLEL)) {
        return parseParallelBlock(parser);
    }

    if (matchToken(parser, TOKEN_ON)) {
        return parseOnFailure(parser);
    }

    // match is handled as an expression (in parsePrimary), not a statement,
    // so it falls through to the expression statement path below.

    if (matchToken(parser, TOKEN_EXIT)) {
        int line = parser->previous.line;
        int col = parser->previous.column;
        AstNode* value = NULL;
        if (!check(parser, TOKEN_NEWLINE) && !check(parser, TOKEN_EOF) &&
            !check(parser, TOKEN_RIGHT_BRACE)) {
            value = parseExpression(parser);
        }
        expectTerminator(parser);
        // exit is sugar for a call to a built-in
        AstNode* exitVar = astNewVariable(parser->arena, "exit", 4, line, col);
        AstNode** args = NULL;
        int argCount = 0;
        if (value) {
            args = (AstNode**)arenaAlloc(parser->arena, sizeof(AstNode*));
            args[0] = value;
            argCount = 1;
        }
        return astNewExprStmt(parser->arena,
            astNewCall(parser->arena, exitVar, args, argCount, line, col),
            line, col);
    }

    // Expression statement
    AstNode* expr = parseExpression(parser);
    int line = expr->line;
    int col = expr->column;

    // Check for assignment: identifier = expr
    if (expr->type == NODE_VARIABLE && matchToken(parser, TOKEN_EQUAL)) {
        AstNode* value = parseExpression(parser);
        expectTerminator(parser);
        return astNewVarDecl(parser->arena,
                             expr->as.variable.name, expr->as.variable.length,
                             value, line, col);
    }

    // Check for compound assignment: identifier += expr, etc.
    if (expr->type == NODE_VARIABLE) {
        TokenType compoundOp = TOKEN_EOF;
        if (matchToken(parser, TOKEN_PLUS_EQUAL)) compoundOp = TOKEN_PLUS_EQUAL;
        else if (matchToken(parser, TOKEN_MINUS_EQUAL)) compoundOp = TOKEN_MINUS_EQUAL;
        else if (matchToken(parser, TOKEN_STAR_EQUAL)) compoundOp = TOKEN_STAR_EQUAL;
        else if (matchToken(parser, TOKEN_SLASH_EQUAL)) compoundOp = TOKEN_SLASH_EQUAL;

        if (compoundOp != TOKEN_EOF) {
            AstNode* value = parseExpression(parser);
            expectTerminator(parser);
            return astNewCompoundAssign(parser->arena,
                expr->as.variable.name, expr->as.variable.length,
                compoundOp, value, line, col);
        }
    }

    expectTerminator(parser);
    return astNewExprStmt(parser->arena, expr, line, col);
}

static AstNode* parseDeclaration(Parser* parser) {
    skipNewlines(parser);

    if (check(parser, TOKEN_EOF)) return NULL;

    AstNode* node = NULL;

    if (parser->panicMode) synchronize(parser);

    if (matchToken(parser, TOKEN_FN)) {
        // Check if this is a declaration (fn name) or lambda (fn()
        if (check(parser, TOKEN_IDENTIFIER)) {
            node = parseFnDeclaration(parser);
        } else {
            // It's a lambda expression used as a statement
            // Put FN back conceptually — we need to parse it as an expression
            // Actually, we already consumed TOKEN_FN. Let me handle this:
            int line = parser->previous.line;
            int col = parser->previous.column;

            consume(parser, TOKEN_LEFT_PAREN, "Expected '(' or function name after 'fn'.");
            ParamList params = parseFnParams(parser);
            skipNewlines(parser);
            AstNode* body = parseBlock(parser);

            AstNode* lambda = astNewLambda(parser->arena, params.names, params.lengths,
                                           params.count, body, line, col);
            lambda = parsePostfix(parser, lambda);
            expectTerminator(parser);
            node = astNewExprStmt(parser->arena, lambda, line, col);
        }
    } else if (matchToken(parser, TOKEN_LET)) {
        int line = parser->previous.line;
        int col = parser->previous.column;
        consume(parser, TOKEN_IDENTIFIER, "Expected variable name after 'let'.");
        const char* name = parser->previous.start;
        int nameLen = parser->previous.length;
        consume(parser, TOKEN_EQUAL, "Expected '=' after variable name.");
        AstNode* initializer = parseExpression(parser);
        expectTerminator(parser);
        node = astNewVarDecl(parser->arena, name, nameLen, initializer, line, col);
    } else if (matchToken(parser, TOKEN_ALLOW)) {
        node = parseAllowDeclaration(parser);
    } else if (matchToken(parser, TOKEN_IMPORT)) {
        node = parseImport(parser);
    } else {
        node = parseStatement(parser);
    }

    if (parser->panicMode) synchronize(parser);

    return node;
}

// ---- Public API ----

AstNode* parse(const char* source, Arena* arena) {
    Parser parser;
    initScanner(&parser.scanner, source);
    parser.arena = arena;
    parser.hadError = false;
    parser.panicMode = false;

    // Prime the parser with the first token
    advanceParser(&parser);

    NodeList stmts;
    nodeListInit(&stmts, arena);

    while (!check(&parser, TOKEN_EOF)) {
        AstNode* decl = parseDeclaration(&parser);
        if (decl != NULL) {
            nodeListAdd(&stmts, decl);
        }
    }

    if (parser.hadError) return NULL;

    return astNewProgram(arena, nodeListFinalize(&stmts, arena),
                         stmts.count, 1, 1);
}
