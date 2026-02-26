#ifndef glipt_ast_h
#define glipt_ast_h

#include "common.h"
#include "token.h"

// Forward declare
typedef struct AstNode AstNode;

typedef enum {
    // Expressions
    NODE_LITERAL,           // number, string, bool, nil
    NODE_UNARY,             // -x, !x, not x
    NODE_BINARY,            // x + y, x == y, x and y
    NODE_VARIABLE,          // identifier reference
    NODE_ASSIGN,            // x = expr
    NODE_COMPOUND_ASSIGN,   // x += expr, x -= expr, etc.
    NODE_CALL,              // f(args)
    NODE_INDEX,             // list[i], map["key"]
    NODE_INDEX_SET,         // list[i] = v
    NODE_DOT,               // config.env
    NODE_DOT_SET,           // config.env = v
    NODE_LIST,              // [1, 2, 3]
    NODE_MAP,               // {"key": value}
    NODE_LAMBDA,            // fn(x) { x + 1 }
    NODE_PIPE,              // expr | fn
    NODE_RANGE,             // 1..10

    // Statements
    NODE_EXPRESSION_STMT,   // expression as statement
    NODE_BLOCK,             // { ... }
    NODE_IF,                // if cond { } else { }
    NODE_WHILE,             // while cond { }
    NODE_FOR,               // for x in iterable { }
    NODE_RETURN,            // return expr
    NODE_BREAK,
    NODE_CONTINUE,

    // Declarations
    NODE_VAR_DECL,          // x = expr / let x = expr
    NODE_FN_DECL,           // fn name(params) { body }

    // Glipt-specific
    NODE_ALLOW,             // allow exec "git"
    NODE_PARALLEL,          // parallel { ... }
    NODE_ON_FAILURE,        // on failure { ... }
    NODE_EXEC,              // exec "command"
    NODE_IMPORT,            // import "module"
    NODE_MATCH,             // match expr { ... }
    NODE_MATCH_ARM,         // pattern -> expr

    // Top-level
    NODE_PROGRAM,           // root node
} NodeType;

// Literal value stored in AST (before VM values exist)
typedef enum {
    LIT_NUMBER,
    LIT_STRING,
    LIT_BOOL,
    LIT_NIL,
} LiteralType;

typedef struct {
    LiteralType type;
    union {
        double number;
        struct {
            const char* chars;
            int length;
            bool isRaw;     // backtick string
        } string;
        bool boolean;
    } as;
} LiteralValue;

// AST Node - tagged union
struct AstNode {
    NodeType type;
    int line;
    int column;

    union {
        // NODE_LITERAL
        struct {
            LiteralValue value;
        } literal;

        // NODE_UNARY
        struct {
            TokenType op;
            AstNode* operand;
        } unary;

        // NODE_BINARY
        struct {
            TokenType op;
            AstNode* left;
            AstNode* right;
        } binary;

        // NODE_VARIABLE
        struct {
            const char* name;
            int length;
        } variable;

        // NODE_ASSIGN
        struct {
            const char* name;
            int length;
            AstNode* value;
        } assign;

        // NODE_COMPOUND_ASSIGN
        struct {
            const char* name;
            int length;
            TokenType op;   // the operator (+=, -=, etc.)
            AstNode* value;
        } compoundAssign;

        // NODE_CALL
        struct {
            AstNode* callee;
            AstNode** args;
            int argCount;
        } call;

        // NODE_INDEX
        struct {
            AstNode* object;
            AstNode* index;
        } index;

        // NODE_INDEX_SET
        struct {
            AstNode* object;
            AstNode* index;
            AstNode* value;
        } indexSet;

        // NODE_DOT
        struct {
            AstNode* object;
            const char* name;
            int nameLength;
        } dot;

        // NODE_DOT_SET
        struct {
            AstNode* object;
            const char* name;
            int nameLength;
            AstNode* value;
        } dotSet;

        // NODE_LIST
        struct {
            AstNode** elements;
            int count;
        } list;

        // NODE_MAP
        struct {
            AstNode** keys;
            AstNode** values;
            int count;
        } map;

        // NODE_LAMBDA, NODE_FN_DECL
        struct {
            const char* name;       // NULL for lambdas
            int nameLength;
            const char** params;
            int* paramLengths;
            int paramCount;
            AstNode* body;          // block node
        } function;

        // NODE_PIPE
        struct {
            AstNode* left;
            AstNode* right;
        } pipe;

        // NODE_RANGE
        struct {
            AstNode* start;
            AstNode* end;
        } range;

        // NODE_EXPRESSION_STMT
        struct {
            AstNode* expression;
        } exprStmt;

        // NODE_BLOCK, NODE_PROGRAM
        struct {
            AstNode** statements;
            int count;
        } block;

        // NODE_IF
        struct {
            AstNode* condition;
            AstNode* thenBranch;    // block
            AstNode* elseBranch;    // block, if-node, or NULL
        } ifStmt;

        // NODE_WHILE
        struct {
            AstNode* condition;
            AstNode* body;          // block
        } whileStmt;

        // NODE_FOR
        struct {
            const char* varName;
            int varNameLength;
            AstNode* iterable;
            AstNode* body;          // block
        } forStmt;

        // NODE_RETURN
        struct {
            AstNode* value;         // NULL if bare return
        } returnStmt;

        // NODE_VAR_DECL
        struct {
            const char* name;
            int length;
            AstNode* initializer;
        } varDecl;

        // NODE_ALLOW
        struct {
            TokenType permType;     // TOKEN_EXEC, TOKEN_NET, TOKEN_READ, TOKEN_WRITE, TOKEN_ENV
            const char* target;
            int targetLength;
        } allow;

        // NODE_PARALLEL
        struct {
            AstNode** tasks;
            int taskCount;
        } parallel;

        // NODE_ON_FAILURE
        struct {
            AstNode* body;          // block
        } onFailure;

        // NODE_EXEC
        struct {
            AstNode* command;       // string expression
            AstNode** args;
            int argCount;
        } exec;

        // NODE_IMPORT
        struct {
            const char* path;
            int pathLength;
            const char* alias;      // NULL if no alias
            int aliasLength;
        } import;

        // NODE_MATCH
        struct {
            AstNode* subject;
            AstNode** arms;
            int armCount;
        } match;

        // NODE_MATCH_ARM
        struct {
            AstNode* pattern;       // expression to compare (or NULL for _)
            AstNode* body;
        } matchArm;
    } as;
};

// ---- Arena Allocator ----

typedef struct ArenaBlock {
    struct ArenaBlock* next;
    size_t size;
    size_t used;
    char data[];
} ArenaBlock;

typedef struct {
    ArenaBlock* current;
    ArenaBlock* head;
    size_t defaultBlockSize;
} Arena;

void arenaInit(Arena* arena, size_t blockSize);
void* arenaAlloc(Arena* arena, size_t size);
void arenaFree(Arena* arena);

// ---- AST Node Constructors ----

AstNode* astNewLiteralNumber(Arena* arena, double value, int line, int col);
AstNode* astNewLiteralString(Arena* arena, const char* chars, int length, bool isRaw, int line, int col);
AstNode* astNewLiteralBool(Arena* arena, bool value, int line, int col);
AstNode* astNewLiteralNil(Arena* arena, int line, int col);
AstNode* astNewUnary(Arena* arena, TokenType op, AstNode* operand, int line, int col);
AstNode* astNewBinary(Arena* arena, TokenType op, AstNode* left, AstNode* right, int line, int col);
AstNode* astNewVariable(Arena* arena, const char* name, int length, int line, int col);
AstNode* astNewAssign(Arena* arena, const char* name, int length, AstNode* value, int line, int col);
AstNode* astNewCompoundAssign(Arena* arena, const char* name, int length, TokenType op, AstNode* value, int line, int col);
AstNode* astNewCall(Arena* arena, AstNode* callee, AstNode** args, int argCount, int line, int col);
AstNode* astNewIndex(Arena* arena, AstNode* object, AstNode* index, int line, int col);
AstNode* astNewIndexSet(Arena* arena, AstNode* object, AstNode* index, AstNode* value, int line, int col);
AstNode* astNewDot(Arena* arena, AstNode* object, const char* name, int nameLength, int line, int col);
AstNode* astNewDotSet(Arena* arena, AstNode* object, const char* name, int nameLength, AstNode* value, int line, int col);
AstNode* astNewList(Arena* arena, AstNode** elements, int count, int line, int col);
AstNode* astNewMap(Arena* arena, AstNode** keys, AstNode** values, int count, int line, int col);
AstNode* astNewLambda(Arena* arena, const char** params, int* paramLengths, int paramCount, AstNode* body, int line, int col);
AstNode* astNewPipe(Arena* arena, AstNode* left, AstNode* right, int line, int col);
AstNode* astNewRange(Arena* arena, AstNode* start, AstNode* end, int line, int col);
AstNode* astNewExprStmt(Arena* arena, AstNode* expr, int line, int col);
AstNode* astNewBlock(Arena* arena, AstNode** stmts, int count, int line, int col);
AstNode* astNewIf(Arena* arena, AstNode* condition, AstNode* thenBranch, AstNode* elseBranch, int line, int col);
AstNode* astNewWhile(Arena* arena, AstNode* condition, AstNode* body, int line, int col);
AstNode* astNewFor(Arena* arena, const char* varName, int varNameLength, AstNode* iterable, AstNode* body, int line, int col);
AstNode* astNewReturn(Arena* arena, AstNode* value, int line, int col);
AstNode* astNewBreak(Arena* arena, int line, int col);
AstNode* astNewContinue(Arena* arena, int line, int col);
AstNode* astNewVarDecl(Arena* arena, const char* name, int length, AstNode* initializer, int line, int col);
AstNode* astNewFnDecl(Arena* arena, const char* name, int nameLength, const char** params, int* paramLengths, int paramCount, AstNode* body, int line, int col);
AstNode* astNewAllow(Arena* arena, TokenType permType, const char* target, int targetLength, int line, int col);
AstNode* astNewParallel(Arena* arena, AstNode** tasks, int taskCount, int line, int col);
AstNode* astNewOnFailure(Arena* arena, AstNode* body, int line, int col);
AstNode* astNewExec(Arena* arena, AstNode* command, AstNode** args, int argCount, int line, int col);
AstNode* astNewImport(Arena* arena, const char* path, int pathLength, const char* alias, int aliasLength, int line, int col);
AstNode* astNewMatch(Arena* arena, AstNode* subject, AstNode** arms, int armCount, int line, int col);
AstNode* astNewMatchArm(Arena* arena, AstNode* pattern, AstNode* body, int line, int col);
AstNode* astNewProgram(Arena* arena, AstNode** stmts, int count, int line, int col);

// ---- Debug ----
void astPrint(AstNode* node, int indent);

#endif
