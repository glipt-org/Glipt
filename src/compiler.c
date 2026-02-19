#include "compiler.h"
#include "ast.h"
#include "parser.h"
#include "memory.h"
#include "debug.h"

typedef enum {
    TYPE_SCRIPT,
    TYPE_FUNCTION,
    TYPE_LAMBDA,
} FunctionType;

typedef struct {
    Token name;
    int depth;
    bool isCaptured;
} Local;

typedef struct {
    uint8_t index;
    bool isLocal;
} Upvalue;

typedef struct Compiler {
    struct Compiler* enclosing;
    ObjFunction* function;
    FunctionType type;

    Local locals[UINT8_COUNT];
    int localCount;
    Upvalue upvalues[UINT8_COUNT];
    int scopeDepth;

    VM* vm;

    // Loop tracking for break/continue
    int loopStart;      // bytecode offset of loop start
    int loopDepth;      // scope depth at loop start

    // Break patches (max 256 breaks per loop)
    int breakJumps[256];
    int breakCount;

    bool hadError;
} Compiler;

static void initCompiler(Compiler* compiler, Compiler* enclosing,
                          FunctionType type, VM* vm) {
    compiler->enclosing = enclosing;
    compiler->function = newFunction(vm);
    compiler->type = type;
    compiler->localCount = 0;
    compiler->scopeDepth = 0;
    compiler->vm = vm;
    compiler->loopStart = -1;
    compiler->loopDepth = 0;
    compiler->breakCount = 0;
    compiler->hadError = false;

    // Reserve slot 0 for the function itself
    Local* local = &compiler->locals[compiler->localCount++];
    local->depth = 0;
    local->isCaptured = false;
    local->name.start = "";
    local->name.length = 0;
}

// ---- Chunk helpers ----

static Chunk* currentChunk(Compiler* compiler) {
    return &compiler->function->chunk;
}

static void emitByte(Compiler* compiler, uint8_t byte, int line) {
    writeChunk(currentChunk(compiler), byte, line);
}

static void emitBytes(Compiler* compiler, uint8_t byte1, uint8_t byte2, int line) {
    emitByte(compiler, byte1, line);
    emitByte(compiler, byte2, line);
}

static int emitJump(Compiler* compiler, uint8_t instruction, int line) {
    emitByte(compiler, instruction, line);
    emitByte(compiler, 0xff, line);
    emitByte(compiler, 0xff, line);
    return currentChunk(compiler)->count - 2;
}

static void patchJump(Compiler* compiler, int offset) {
    int jump = currentChunk(compiler)->count - offset - 2;

    if (jump > UINT16_MAX) {
        fprintf(stderr, "Too much code to jump over.\n");
        return;
    }

    currentChunk(compiler)->code[offset] = (jump >> 8) & 0xff;
    currentChunk(compiler)->code[offset + 1] = jump & 0xff;
}

static void emitLoop(Compiler* compiler, int loopStart, int line) {
    emitByte(compiler, OP_LOOP, line);

    int offset = currentChunk(compiler)->count - loopStart + 2;
    if (offset > UINT16_MAX) {
        fprintf(stderr, "Loop body too large.\n");
    }

    emitByte(compiler, (offset >> 8) & 0xff, line);
    emitByte(compiler, offset & 0xff, line);
}

static uint8_t makeConstant(Compiler* compiler, Value value) {
    int constant = addConstant(currentChunk(compiler), value);
    if (constant > UINT8_MAX) {
        fprintf(stderr, "Too many constants in one chunk.\n");
        compiler->hadError = true;
        return 0;
    }
    return (uint8_t)constant;
}

static void emitConstant(Compiler* compiler, Value value, int line) {
    emitBytes(compiler, OP_CONSTANT, makeConstant(compiler, value), line);
}

static void emitReturn(Compiler* compiler, int line) {
    emitByte(compiler, OP_NIL, line);
    emitByte(compiler, OP_RETURN, line);
}

// ---- Scope Management ----

static void beginScope(Compiler* compiler) {
    compiler->scopeDepth++;
}

static void endScope(Compiler* compiler, int line) {
    compiler->scopeDepth--;

    while (compiler->localCount > 0 &&
           compiler->locals[compiler->localCount - 1].depth > compiler->scopeDepth) {
        if (compiler->locals[compiler->localCount - 1].isCaptured) {
            emitByte(compiler, OP_CLOSE_UPVALUE, line);
        } else {
            emitByte(compiler, OP_POP, line);
        }
        compiler->localCount--;
    }
}

// ---- Variable Resolution ----

static void addLocal(Compiler* compiler, const char* name, int length) {
    if (compiler->localCount == UINT8_COUNT) {
        fprintf(stderr, "Too many local variables in function.\n");
        return;
    }
    Local* local = &compiler->locals[compiler->localCount++];
    local->name.start = name;
    local->name.length = length;
    local->depth = compiler->scopeDepth;
    local->isCaptured = false;
}

static int resolveLocal(Compiler* compiler, const char* name, int length) {
    for (int i = compiler->localCount - 1; i >= 0; i--) {
        Local* local = &compiler->locals[i];
        if (local->name.length == length &&
            memcmp(local->name.start, name, length) == 0) {
            return i;
        }
    }
    return -1;
}

static int addUpvalue(Compiler* compiler, uint8_t index, bool isLocal) {
    int upvalueCount = compiler->function->upvalueCount;

    // Check if we already have this upvalue
    for (int i = 0; i < upvalueCount; i++) {
        Upvalue* upvalue = &compiler->upvalues[i];
        if (upvalue->index == index && upvalue->isLocal == isLocal) {
            return i;
        }
    }

    if (upvalueCount == UINT8_COUNT) {
        fprintf(stderr, "Too many closure variables in function.\n");
        return 0;
    }

    compiler->upvalues[upvalueCount].isLocal = isLocal;
    compiler->upvalues[upvalueCount].index = index;
    return compiler->function->upvalueCount++;
}

static int resolveUpvalue(Compiler* compiler, const char* name, int length) {
    if (compiler->enclosing == NULL) return -1;

    int local = resolveLocal(compiler->enclosing, name, length);
    if (local != -1) {
        compiler->enclosing->locals[local].isCaptured = true;
        return addUpvalue(compiler, (uint8_t)local, true);
    }

    int upvalue = resolveUpvalue(compiler->enclosing, name, length);
    if (upvalue != -1) {
        return addUpvalue(compiler, (uint8_t)upvalue, false);
    }

    return -1;
}

static uint8_t identifierConstant(Compiler* compiler, const char* name, int length) {
    return makeConstant(compiler,
        OBJ_VAL(copyString(compiler->vm, name, length)));
}

// ---- Compile AST Nodes ----

static void compileNode(Compiler* compiler, AstNode* node);
static void compileExpression(Compiler* compiler, AstNode* node);

static void compileExpression(Compiler* compiler, AstNode* node) {
    compileNode(compiler, node);
}

static void compileLiteral(Compiler* compiler, AstNode* node) {
    int line = node->line;
    LiteralValue* lit = &node->as.literal.value;

    switch (lit->type) {
        case LIT_NUMBER:
            emitConstant(compiler, NUMBER_VAL(lit->as.number), line);
            break;
        case LIT_STRING: {
            ObjString* str = copyString(compiler->vm,
                lit->as.string.chars, lit->as.string.length);
            emitConstant(compiler, OBJ_VAL(str), line);
            break;
        }
        case LIT_BOOL:
            emitByte(compiler, lit->as.boolean ? OP_TRUE : OP_FALSE, line);
            break;
        case LIT_NIL:
            emitByte(compiler, OP_NIL, line);
            break;
    }
}

static void compileVariable(Compiler* compiler, AstNode* node, bool forGet) {
    int line = node->line;
    const char* name;
    int length;

    if (node->type == NODE_VARIABLE) {
        name = node->as.variable.name;
        length = node->as.variable.length;
    } else {
        return;
    }

    int local = resolveLocal(compiler, name, length);
    if (local != -1) {
        if (forGet) {
            emitBytes(compiler, OP_GET_LOCAL, (uint8_t)local, line);
        } else {
            emitBytes(compiler, OP_SET_LOCAL, (uint8_t)local, line);
        }
    } else {
        int upvalue = resolveUpvalue(compiler, name, length);
        if (upvalue != -1) {
            if (forGet) {
                emitBytes(compiler, OP_GET_UPVALUE, (uint8_t)upvalue, line);
            } else {
                emitBytes(compiler, OP_SET_UPVALUE, (uint8_t)upvalue, line);
            }
        } else {
            uint8_t arg = identifierConstant(compiler, name, length);
            if (forGet) {
                emitBytes(compiler, OP_GET_GLOBAL, arg, line);
            } else {
                emitBytes(compiler, OP_SET_GLOBAL, arg, line);
            }
        }
    }
}

static void compileUnary(Compiler* compiler, AstNode* node) {
    int line = node->line;
    compileExpression(compiler, node->as.unary.operand);

    switch (node->as.unary.op) {
        case TOKEN_MINUS: emitByte(compiler, OP_NEGATE, line); break;
        case TOKEN_BANG:
        case TOKEN_NOT:   emitByte(compiler, OP_NOT, line); break;
        default: break;
    }
}

static void compileBinary(Compiler* compiler, AstNode* node) {
    int line = node->line;
    TokenType op = node->as.binary.op;

    // Short-circuit for 'and' / '&&'
    if (op == TOKEN_AND || op == TOKEN_AMP_AMP) {
        compileExpression(compiler, node->as.binary.left);
        int endJump = emitJump(compiler, OP_JUMP_IF_FALSE, line);
        emitByte(compiler, OP_POP, line);
        compileExpression(compiler, node->as.binary.right);
        patchJump(compiler, endJump);
        return;
    }

    // Short-circuit for 'or' / '||'
    if (op == TOKEN_OR || op == TOKEN_PIPE_PIPE) {
        compileExpression(compiler, node->as.binary.left);
        int elseJump = emitJump(compiler, OP_JUMP_IF_FALSE, line);
        int endJump = emitJump(compiler, OP_JUMP, line);
        patchJump(compiler, elseJump);
        emitByte(compiler, OP_POP, line);
        compileExpression(compiler, node->as.binary.right);
        patchJump(compiler, endJump);
        return;
    }

    compileExpression(compiler, node->as.binary.left);
    compileExpression(compiler, node->as.binary.right);

    switch (op) {
        case TOKEN_PLUS:          emitByte(compiler, OP_ADD, line); break;
        case TOKEN_MINUS:         emitByte(compiler, OP_SUBTRACT, line); break;
        case TOKEN_STAR:          emitByte(compiler, OP_MULTIPLY, line); break;
        case TOKEN_SLASH:         emitByte(compiler, OP_DIVIDE, line); break;
        case TOKEN_PERCENT:       emitByte(compiler, OP_MODULO, line); break;
        case TOKEN_EQUAL_EQUAL:   emitByte(compiler, OP_EQUAL, line); break;
        case TOKEN_BANG_EQUAL:    emitByte(compiler, OP_NOT_EQUAL, line); break;
        case TOKEN_GREATER:       emitByte(compiler, OP_GREATER, line); break;
        case TOKEN_GREATER_EQUAL: emitByte(compiler, OP_GREATER_EQUAL, line); break;
        case TOKEN_LESS:          emitByte(compiler, OP_LESS, line); break;
        case TOKEN_LESS_EQUAL:    emitByte(compiler, OP_LESS_EQUAL, line); break;
        default: break;
    }
}

static void compileCall(Compiler* compiler, AstNode* node) {
    int line = node->line;
    compileExpression(compiler, node->as.call.callee);

    int argCount = node->as.call.argCount;
    if (argCount > UINT8_MAX) {
        fprintf(stderr, "[line %d] Error: Can't have more than 255 arguments.\n", line);
        compiler->hadError = true;
        argCount = UINT8_MAX;
    }

    for (int i = 0; i < node->as.call.argCount; i++) {
        compileExpression(compiler, node->as.call.args[i]);
    }

    emitBytes(compiler, OP_CALL, (uint8_t)argCount, line);
}

static void compileList(Compiler* compiler, AstNode* node) {
    int line = node->line;
    int count = node->as.list.count;
    if (count > UINT8_MAX) {
        fprintf(stderr, "[line %d] Error: Can't have more than 255 list elements.\n", line);
        compiler->hadError = true;
        count = UINT8_MAX;
    }

    for (int i = 0; i < node->as.list.count; i++) {
        compileExpression(compiler, node->as.list.elements[i]);
    }
    emitBytes(compiler, OP_BUILD_LIST, (uint8_t)count, line);
}

static void compileMap(Compiler* compiler, AstNode* node) {
    int line = node->line;
    int count = node->as.map.count;
    if (count > UINT8_MAX) {
        fprintf(stderr, "[line %d] Error: Can't have more than 255 map entries.\n", line);
        compiler->hadError = true;
        count = UINT8_MAX;
    }

    for (int i = 0; i < node->as.map.count; i++) {
        compileExpression(compiler, node->as.map.keys[i]);
        compileExpression(compiler, node->as.map.values[i]);
    }
    emitBytes(compiler, OP_BUILD_MAP, (uint8_t)count, line);
}

static void compileIndex(Compiler* compiler, AstNode* node) {
    int line = node->line;
    compileExpression(compiler, node->as.index.object);
    compileExpression(compiler, node->as.index.index);
    emitByte(compiler, OP_INDEX_GET, line);
}

static void compileIndexSet(Compiler* compiler, AstNode* node) {
    int line = node->line;
    compileExpression(compiler, node->as.indexSet.object);
    compileExpression(compiler, node->as.indexSet.index);
    compileExpression(compiler, node->as.indexSet.value);
    emitByte(compiler, OP_INDEX_SET, line);
}

static void compileDot(Compiler* compiler, AstNode* node) {
    int line = node->line;
    compileExpression(compiler, node->as.dot.object);
    uint8_t name = identifierConstant(compiler,
        node->as.dot.name, node->as.dot.nameLength);
    emitBytes(compiler, OP_GET_PROPERTY, name, line);
}

static void compileDotSet(Compiler* compiler, AstNode* node) {
    int line = node->line;
    compileExpression(compiler, node->as.dotSet.object);
    compileExpression(compiler, node->as.dotSet.value);
    uint8_t name = identifierConstant(compiler,
        node->as.dotSet.name, node->as.dotSet.nameLength);
    emitBytes(compiler, OP_SET_PROPERTY, name, line);
}

// Forward declaration
static void compileStatements(Compiler* compiler, AstNode** stmts, int count);

static ObjFunction* compileFunction(Compiler* parent, AstNode* node,
                                      FunctionType type) {
    Compiler compiler;
    initCompiler(&compiler, parent, type, parent->vm);

    // Set function name
    if (node->as.function.name != NULL) {
        compiler.function->name = copyString(parent->vm,
            node->as.function.name, node->as.function.nameLength);
    }

    beginScope(&compiler);

    // Bind parameters
    compiler.function->arity = node->as.function.paramCount;
    for (int i = 0; i < node->as.function.paramCount; i++) {
        addLocal(&compiler,
                 node->as.function.params[i],
                 node->as.function.paramLengths[i]);
    }

    // Compile body (should be a block node)
    AstNode* body = node->as.function.body;
    if (body->type == NODE_BLOCK) {
        compileStatements(&compiler, body->as.block.statements, body->as.block.count);
    } else {
        compileNode(&compiler, body);
    }

    // Implicit return nil
    emitReturn(&compiler, node->line);

    ObjFunction* function = compiler.function;

#ifdef DEBUG_TRACE
    if (function->name != NULL) {
        disassembleChunk(currentChunk(&compiler), function->name->chars);
    } else {
        disassembleChunk(currentChunk(&compiler), "<script>");
    }
#endif

    // Emit closure instruction in the enclosing compiler
    emitBytes(parent, OP_CLOSURE,
              makeConstant(parent, OBJ_VAL(function)), node->line);

    for (int i = 0; i < function->upvalueCount; i++) {
        emitByte(parent, compiler.upvalues[i].isLocal ? 1 : 0, node->line);
        emitByte(parent, compiler.upvalues[i].index, node->line);
    }

    return function;
}

// Compile a list of statements, handling 'on failure' blocks by wrapping
// subsequent statements in error handler protection.
static void compileStatements(Compiler* compiler, AstNode** stmts, int count) {
    for (int i = 0; i < count; i++) {
        if (stmts[i]->type == NODE_ON_FAILURE) {
            int line = stmts[i]->line;
            // OP_PUSH_HANDLER: jump to handler body on error
            int handlerJump = emitJump(compiler, OP_PUSH_HANDLER, line);

            // Compile all remaining statements as protected code
            for (int j = i + 1; j < count; j++) {
                compileNode(compiler, stmts[j]);
            }

            // Success: pop handler and jump past handler body
            emitByte(compiler, OP_POP_HANDLER, line);
            int endJump = emitJump(compiler, OP_JUMP, line);

            // Handler entry point
            patchJump(compiler, handlerJump);
            // Error value is on the stack - define 'error' local
            beginScope(compiler);
            addLocal(compiler, "error", 5);

            AstNode* body = stmts[i]->as.onFailure.body;
            if (body->type == NODE_BLOCK) {
                for (int j = 0; j < body->as.block.count; j++) {
                    compileNode(compiler, body->as.block.statements[j]);
                }
            } else {
                compileNode(compiler, body);
            }
            endScope(compiler, line);

            // End of handler
            patchJump(compiler, endJump);

            // All remaining statements were compiled above, so stop
            return;
        }
        compileNode(compiler, stmts[i]);
    }
}

static void compileBlock(Compiler* compiler, AstNode* node) {
    beginScope(compiler);
    compileStatements(compiler, node->as.block.statements, node->as.block.count);
    endScope(compiler, node->line);
}

static void compileIf(Compiler* compiler, AstNode* node) {
    int line = node->line;

    compileExpression(compiler, node->as.ifStmt.condition);
    int thenJump = emitJump(compiler, OP_JUMP_IF_FALSE, line);
    emitByte(compiler, OP_POP, line); // pop condition

    // Then branch
    if (node->as.ifStmt.thenBranch->type == NODE_BLOCK) {
        compileBlock(compiler, node->as.ifStmt.thenBranch);
    } else {
        compileNode(compiler, node->as.ifStmt.thenBranch);
    }

    int elseJump = emitJump(compiler, OP_JUMP, line);
    patchJump(compiler, thenJump);
    emitByte(compiler, OP_POP, line); // pop condition

    // Else branch
    if (node->as.ifStmt.elseBranch != NULL) {
        compileNode(compiler, node->as.ifStmt.elseBranch);
    }

    patchJump(compiler, elseJump);
}

static void compileWhile(Compiler* compiler, AstNode* node) {
    int line = node->line;

    // Save previous loop state
    int prevLoopStart = compiler->loopStart;
    int prevLoopDepth = compiler->loopDepth;
    int prevBreakCount = compiler->breakCount;

    int loopStart = currentChunk(compiler)->count;
    compiler->loopStart = loopStart;
    compiler->loopDepth = compiler->scopeDepth;
    compiler->breakCount = 0;

    compileExpression(compiler, node->as.whileStmt.condition);
    int exitJump = emitJump(compiler, OP_JUMP_IF_FALSE, line);
    emitByte(compiler, OP_POP, line);

    // Body
    if (node->as.whileStmt.body->type == NODE_BLOCK) {
        compileBlock(compiler, node->as.whileStmt.body);
    } else {
        compileNode(compiler, node->as.whileStmt.body);
    }

    emitLoop(compiler, loopStart, line);

    patchJump(compiler, exitJump);
    emitByte(compiler, OP_POP, line);

    // Patch all breaks
    for (int i = 0; i < compiler->breakCount; i++) {
        patchJump(compiler, compiler->breakJumps[i]);
    }

    // Restore loop state
    compiler->loopStart = prevLoopStart;
    compiler->loopDepth = prevLoopDepth;
    compiler->breakCount = prevBreakCount;
}

static void compileFor(Compiler* compiler, AstNode* node) {
    int line = node->line;

    // Save loop state
    int prevLoopStart = compiler->loopStart;
    int prevLoopDepth = compiler->loopDepth;
    int prevBreakCount = compiler->breakCount;

    beginScope(compiler);

    // 1. Evaluate iterable and store as hidden local
    compileExpression(compiler, node->as.forStmt.iterable);
    addLocal(compiler, " iterable", 9);
    int iterSlot = compiler->localCount - 1;

    // 2. Index counter as hidden local, initialized to 0
    emitConstant(compiler, NUMBER_VAL(0), line);
    addLocal(compiler, " index", 6);
    int idxSlot = compiler->localCount - 1;

    // 3. Loop variable, initialized to nil
    emitByte(compiler, OP_NIL, line);
    addLocal(compiler, node->as.forStmt.varName, node->as.forStmt.varNameLength);
    int varSlot = compiler->localCount - 1;

    int loopStart = currentChunk(compiler)->count;
    compiler->loopStart = loopStart;
    compiler->loopDepth = compiler->scopeDepth;
    compiler->breakCount = 0;

    // Condition: index < iterable.length
    emitBytes(compiler, OP_GET_LOCAL, (uint8_t)idxSlot, line);
    emitBytes(compiler, OP_GET_LOCAL, (uint8_t)iterSlot, line);
    uint8_t lengthConst = identifierConstant(compiler, "length", 6);
    emitBytes(compiler, OP_GET_PROPERTY, lengthConst, line);
    emitByte(compiler, OP_LESS, line);

    int exitJump = emitJump(compiler, OP_JUMP_IF_FALSE, line);
    emitByte(compiler, OP_POP, line);

    // Set loop var = iterable[index]
    emitBytes(compiler, OP_GET_LOCAL, (uint8_t)iterSlot, line);
    emitBytes(compiler, OP_GET_LOCAL, (uint8_t)idxSlot, line);
    emitByte(compiler, OP_INDEX_GET, line);
    emitBytes(compiler, OP_SET_LOCAL, (uint8_t)varSlot, line);
    emitByte(compiler, OP_POP, line);

    // Compile body
    if (node->as.forStmt.body->type == NODE_BLOCK) {
        AstNode* body = node->as.forStmt.body;
        beginScope(compiler);
        for (int i = 0; i < body->as.block.count; i++) {
            compileNode(compiler, body->as.block.statements[i]);
        }
        endScope(compiler, line);
    } else {
        compileNode(compiler, node->as.forStmt.body);
    }

    // Increment index: index = index + 1
    emitBytes(compiler, OP_GET_LOCAL, (uint8_t)idxSlot, line);
    emitConstant(compiler, NUMBER_VAL(1), line);
    emitByte(compiler, OP_ADD, line);
    emitBytes(compiler, OP_SET_LOCAL, (uint8_t)idxSlot, line);
    emitByte(compiler, OP_POP, line);

    emitLoop(compiler, loopStart, line);

    patchJump(compiler, exitJump);
    emitByte(compiler, OP_POP, line);

    // Patch breaks
    for (int i = 0; i < compiler->breakCount; i++) {
        patchJump(compiler, compiler->breakJumps[i]);
    }

    endScope(compiler, line);

    // Restore loop state
    compiler->loopStart = prevLoopStart;
    compiler->loopDepth = prevLoopDepth;
    compiler->breakCount = prevBreakCount;
}

static void compileVarDecl(Compiler* compiler, AstNode* node) {
    int line = node->line;
    const char* name = node->as.varDecl.name;
    int length = node->as.varDecl.length;

    compileExpression(compiler, node->as.varDecl.initializer);

    if (compiler->scopeDepth > 0) {
        // Check if variable already exists: local → upvalue → global
        int local = resolveLocal(compiler, name, length);
        if (local != -1) {
            emitBytes(compiler, OP_SET_LOCAL, (uint8_t)local, line);
            emitByte(compiler, OP_POP, line);
            return;
        }
        int upvalue = resolveUpvalue(compiler, name, length);
        if (upvalue != -1) {
            emitBytes(compiler, OP_SET_UPVALUE, (uint8_t)upvalue, line);
            emitByte(compiler, OP_POP, line);
            return;
        }
        // If inside a function (not just a block), create a new local.
        // If inside a block within the top-level (e.g. for/while body
        // at global scope), set as global to allow mutation of outer vars.
        if (compiler->enclosing != NULL) {
            // We're inside a function - create new local
            addLocal(compiler, name, length);
        } else {
            // Top-level block (for/while/if body) - set global
            uint8_t global = identifierConstant(compiler, name, length);
            emitBytes(compiler, OP_SET_GLOBAL, global, line);
            emitByte(compiler, OP_POP, line);
        }
    } else {
        // Global scope - define global
        uint8_t global = identifierConstant(compiler, name, length);
        emitBytes(compiler, OP_DEFINE_GLOBAL, global, line);
    }
}

static void compileAssign(Compiler* compiler, AstNode* node) {
    int line = node->line;
    compileExpression(compiler, node->as.assign.value);

    int local = resolveLocal(compiler, node->as.assign.name, node->as.assign.length);
    if (local != -1) {
        emitBytes(compiler, OP_SET_LOCAL, (uint8_t)local, line);
    } else {
        int upvalue = resolveUpvalue(compiler, node->as.assign.name, node->as.assign.length);
        if (upvalue != -1) {
            emitBytes(compiler, OP_SET_UPVALUE, (uint8_t)upvalue, line);
        } else {
            uint8_t arg = identifierConstant(compiler,
                node->as.assign.name, node->as.assign.length);
            emitBytes(compiler, OP_SET_GLOBAL, arg, line);
        }
    }
}

static void compileCompoundAssign(Compiler* compiler, AstNode* node) {
    int line = node->line;
    const char* name = node->as.compoundAssign.name;
    int length = node->as.compoundAssign.length;

    // Get current value
    int local = resolveLocal(compiler, name, length);
    if (local != -1) {
        emitBytes(compiler, OP_GET_LOCAL, (uint8_t)local, line);
    } else {
        int upvalue = resolveUpvalue(compiler, name, length);
        if (upvalue != -1) {
            emitBytes(compiler, OP_GET_UPVALUE, (uint8_t)upvalue, line);
        } else {
            uint8_t arg = identifierConstant(compiler, name, length);
            emitBytes(compiler, OP_GET_GLOBAL, arg, line);
        }
    }

    // Compile the right-hand side
    compileExpression(compiler, node->as.compoundAssign.value);

    // Apply the operator
    switch (node->as.compoundAssign.op) {
        case TOKEN_PLUS_EQUAL:  emitByte(compiler, OP_ADD, line); break;
        case TOKEN_MINUS_EQUAL: emitByte(compiler, OP_SUBTRACT, line); break;
        case TOKEN_STAR_EQUAL:  emitByte(compiler, OP_MULTIPLY, line); break;
        case TOKEN_SLASH_EQUAL: emitByte(compiler, OP_DIVIDE, line); break;
        default: break;
    }

    // Set the variable
    if (local != -1) {
        emitBytes(compiler, OP_SET_LOCAL, (uint8_t)local, line);
    } else {
        int upvalue = resolveUpvalue(compiler, name, length);
        if (upvalue != -1) {
            emitBytes(compiler, OP_SET_UPVALUE, (uint8_t)upvalue, line);
        } else {
            uint8_t arg = identifierConstant(compiler, name, length);
            emitBytes(compiler, OP_SET_GLOBAL, arg, line);
        }
    }
}

static void compilePipe(Compiler* compiler, AstNode* node) {
    int line = node->line;
    // Pipe: left | right
    // Desugar to: right(left)
    compileExpression(compiler, node->as.pipe.right); // push function
    compileExpression(compiler, node->as.pipe.left);  // push argument
    // Swap: we need callee below argument
    // Actually, for OP_CALL, the callee is below the args on the stack.
    // So we need: [callee, arg] = [right, left]
    // We pushed right first, then left. Stack is: ... right left
    // We need: ... right left -> CALL 1
    // But CALL expects: ... callee arg1 arg2...
    // So stack is: right, left. CALL(1) will use peek(1) = right as callee. Correct!
    emitBytes(compiler, OP_CALL, 1, line);
}

static void compileNode(Compiler* compiler, AstNode* node) {
    if (node == NULL) return;

    switch (node->type) {
        case NODE_LITERAL:
            compileLiteral(compiler, node);
            break;

        case NODE_VARIABLE:
            compileVariable(compiler, node, true);
            break;

        case NODE_UNARY:
            compileUnary(compiler, node);
            break;

        case NODE_BINARY:
            compileBinary(compiler, node);
            break;

        case NODE_CALL:
            compileCall(compiler, node);
            break;

        case NODE_LIST:
            compileList(compiler, node);
            break;

        case NODE_MAP:
            compileMap(compiler, node);
            break;

        case NODE_INDEX:
            compileIndex(compiler, node);
            break;

        case NODE_INDEX_SET:
            compileIndexSet(compiler, node);
            break;

        case NODE_DOT:
            compileDot(compiler, node);
            break;

        case NODE_DOT_SET:
            compileDotSet(compiler, node);
            break;

        case NODE_ASSIGN:
            compileAssign(compiler, node);
            break;

        case NODE_COMPOUND_ASSIGN:
            compileCompoundAssign(compiler, node);
            break;

        case NODE_PIPE:
            compilePipe(compiler, node);
            break;

        case NODE_RANGE:
            // Range literal: currently only compiles start expression
            compileExpression(compiler, node->as.range.start);
            break;

        case NODE_LAMBDA: {
            compileFunction(compiler, node, TYPE_LAMBDA);
            break;
        }

        case NODE_EXPRESSION_STMT:
            compileExpression(compiler, node->as.exprStmt.expression);
            // Check if the expression is a call to 'print' - if so, use OP_PRINT
            // Actually, let print be a native function. Just pop the result.
            emitByte(compiler, OP_POP, node->line);
            break;

        case NODE_BLOCK:
            compileBlock(compiler, node);
            break;

        case NODE_VAR_DECL:
            compileVarDecl(compiler, node);
            break;

        case NODE_FN_DECL: {
            int line = node->line;
            const char* name = node->as.function.name;
            int nameLen = node->as.function.nameLength;

            compileFunction(compiler, node, TYPE_FUNCTION);

            if (compiler->scopeDepth > 0) {
                addLocal(compiler, name, nameLen);
            } else {
                uint8_t global = identifierConstant(compiler, name, nameLen);
                emitBytes(compiler, OP_DEFINE_GLOBAL, global, line);
            }
            break;
        }

        case NODE_IF:
            compileIf(compiler, node);
            break;

        case NODE_WHILE:
            compileWhile(compiler, node);
            break;

        case NODE_FOR:
            compileFor(compiler, node);
            break;

        case NODE_RETURN: {
            int line = node->line;
            if (compiler->type == TYPE_SCRIPT) {
                fprintf(stderr, "[line %d] Error: Can't return from top-level code.\n", line);
            }
            if (node->as.returnStmt.value != NULL) {
                compileExpression(compiler, node->as.returnStmt.value);
            } else {
                emitByte(compiler, OP_NIL, line);
            }
            emitByte(compiler, OP_RETURN, line);
            break;
        }

        case NODE_BREAK: {
            int line = node->line;
            if (compiler->loopStart == -1) {
                fprintf(stderr, "[line %d] Error: Can't use 'break' outside a loop.\n", line);
                compiler->hadError = true;
                break;
            }
            // Emit jump (will be patched later)
            if (compiler->breakCount >= 256) {
                fprintf(stderr, "[line %d] Error: Too many break statements in loop.\n", line);
                compiler->hadError = true;
                break;
            }
            compiler->breakJumps[compiler->breakCount++] = emitJump(compiler, OP_JUMP, line);
            break;
        }

        case NODE_CONTINUE: {
            int line = node->line;
            if (compiler->loopStart == -1) {
                fprintf(stderr, "[line %d] Error: Can't use 'continue' outside a loop.\n", line);
                compiler->hadError = true;
                break;
            }
            emitLoop(compiler, compiler->loopStart, line);
            break;
        }

        case NODE_ALLOW: {
            // Map token type to permission type byte
            uint8_t permType;
            switch (node->as.allow.permType) {
                case TOKEN_EXEC:  permType = 0; break; // PERM_EXEC
                case TOKEN_NET:   permType = 1; break; // PERM_NET
                case TOKEN_READ:  permType = 2; break; // PERM_READ
                case TOKEN_WRITE: permType = 3; break; // PERM_WRITE
                case TOKEN_ENV:   permType = 4; break; // PERM_ENV
                default:          permType = 0; break;
            }
            uint8_t target = makeConstant(compiler,
                OBJ_VAL(copyString(compiler->vm, node->as.allow.target, node->as.allow.targetLength)));
            emitByte(compiler, OP_ALLOW, node->line);
            emitByte(compiler, permType, node->line);
            emitByte(compiler, target, node->line);
            break;
        }

        case NODE_PARALLEL:
            // Parallel blocks: execute tasks sequentially (use parallel_exec() native for true parallelism)
            for (int i = 0; i < node->as.parallel.taskCount; i++) {
                compileNode(compiler, node->as.parallel.tasks[i]);
            }
            break;

        case NODE_ON_FAILURE:
            // Handled by compileStatements() at the block level
            break;

        case NODE_EXEC: {
            // Compile as: exec(command)
            int line = node->line;
            uint8_t execGlobal = identifierConstant(compiler, "exec", 4);
            emitBytes(compiler, OP_GET_GLOBAL, execGlobal, line);
            compileExpression(compiler, node->as.exec.command);
            emitBytes(compiler, OP_CALL, 1, line);
            break;
        }

        case NODE_IMPORT:
            // Import statements: not yet implemented
            break;

        case NODE_MATCH: {
            int line = node->line;
            // Compile subject once
            compileExpression(compiler, node->as.match.subject);

            int armCount = node->as.match.armCount;
            int endJumps[256];
            int endJumpCount = 0;

            for (int i = 0; i < armCount; i++) {
                AstNode* arm = node->as.match.arms[i];
                if (arm->as.matchArm.pattern == NULL) {
                    // Wildcard: always matches
                    emitByte(compiler, OP_POP, line); // pop subject
                    compileExpression(compiler, arm->as.matchArm.body);
                    break;
                }

                // JUMP_IF_FALSE doesn't pop, so we can re-examine.
                // But EQUAL pops both. We need to re-push the subject.
                // Simple workaround: store subject in a hidden local.

                // This needs more thought, skip match for now and implement it later.
                (void)arm;
                (void)endJumps;
                (void)endJumpCount;
            }

            // Clean up subject from stack after all arms
            emitByte(compiler, OP_POP, line);
            break;
        }

        case NODE_MATCH_ARM:
            // Handled inside NODE_MATCH
            break;

        case NODE_PROGRAM:
            compileStatements(compiler, node->as.block.statements, node->as.block.count);
            break;
    }
}

// ---- Public API ----

ObjFunction* compile(VM* vm, const char* source) {
    Arena arena;
    arenaInit(&arena, 0);

    AstNode* program = parse(source, &arena);
    if (program == NULL) {
        arenaFree(&arena);
        return NULL;
    }

    Compiler compiler;
    initCompiler(&compiler, NULL, TYPE_SCRIPT, vm);

    compileNode(&compiler, program);

    emitReturn(&compiler, 0);

    ObjFunction* function = compiler.function;

#ifdef DEBUG_TRACE
    disassembleChunk(currentChunk(&compiler), "<script>");
#endif

    arenaFree(&arena);

    if (compiler.hadError) return NULL;

    return function;
}
