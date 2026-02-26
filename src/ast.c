#include "ast.h"

// ---- Arena Allocator ----

#define DEFAULT_BLOCK_SIZE (1024 * 64) // 64 KB

void arenaInit(Arena* arena, size_t blockSize) {
    arena->defaultBlockSize = blockSize > 0 ? blockSize : DEFAULT_BLOCK_SIZE;
    arena->head = NULL;
    arena->current = NULL;
}

static ArenaBlock* arenaNewBlock(Arena* arena, size_t minSize) {
    size_t size = arena->defaultBlockSize;
    if (minSize > size) size = minSize;

    ArenaBlock* block = (ArenaBlock*)malloc(sizeof(ArenaBlock) + size);
    if (block == NULL) {
        fprintf(stderr, "Error: Arena out of memory.\n");
        exit(1);
    }
    block->size = size;
    block->used = 0;
    block->next = NULL;

    if (arena->current != NULL) {
        arena->current->next = block;
    }
    arena->current = block;
    if (arena->head == NULL) {
        arena->head = block;
    }

    return block;
}

void* arenaAlloc(Arena* arena, size_t size) {
    // Align to 8 bytes
    size = (size + 7) & ~((size_t)7);

    if (arena->current == NULL || arena->current->used + size > arena->current->size) {
        arenaNewBlock(arena, size);
    }

    void* ptr = arena->current->data + arena->current->used;
    arena->current->used += size;
    return ptr;
}

void arenaFree(Arena* arena) {
    ArenaBlock* block = arena->head;
    while (block != NULL) {
        ArenaBlock* next = block->next;
        free(block);
        block = next;
    }
    arena->head = NULL;
    arena->current = NULL;
}

// ---- AST Node Constructors ----

static AstNode* newNode(Arena* arena, NodeType type, int line, int col) {
    AstNode* node = (AstNode*)arenaAlloc(arena, sizeof(AstNode));
    memset(node, 0, sizeof(AstNode));
    node->type = type;
    node->line = line;
    node->column = col;
    return node;
}

AstNode* astNewLiteralNumber(Arena* arena, double value, int line, int col) {
    AstNode* node = newNode(arena, NODE_LITERAL, line, col);
    node->as.literal.value.type = LIT_NUMBER;
    node->as.literal.value.as.number = value;
    return node;
}

AstNode* astNewLiteralString(Arena* arena, const char* chars, int length, bool isRaw, int line, int col) {
    AstNode* node = newNode(arena, NODE_LITERAL, line, col);
    node->as.literal.value.type = LIT_STRING;
    node->as.literal.value.as.string.chars = chars;
    node->as.literal.value.as.string.length = length;
    node->as.literal.value.as.string.isRaw = isRaw;
    return node;
}

AstNode* astNewLiteralBool(Arena* arena, bool value, int line, int col) {
    AstNode* node = newNode(arena, NODE_LITERAL, line, col);
    node->as.literal.value.type = LIT_BOOL;
    node->as.literal.value.as.boolean = value;
    return node;
}

AstNode* astNewLiteralNil(Arena* arena, int line, int col) {
    AstNode* node = newNode(arena, NODE_LITERAL, line, col);
    node->as.literal.value.type = LIT_NIL;
    return node;
}

AstNode* astNewUnary(Arena* arena, TokenType op, AstNode* operand, int line, int col) {
    AstNode* node = newNode(arena, NODE_UNARY, line, col);
    node->as.unary.op = op;
    node->as.unary.operand = operand;
    return node;
}

AstNode* astNewBinary(Arena* arena, TokenType op, AstNode* left, AstNode* right, int line, int col) {
    AstNode* node = newNode(arena, NODE_BINARY, line, col);
    node->as.binary.op = op;
    node->as.binary.left = left;
    node->as.binary.right = right;
    return node;
}

AstNode* astNewVariable(Arena* arena, const char* name, int length, int line, int col) {
    AstNode* node = newNode(arena, NODE_VARIABLE, line, col);
    node->as.variable.name = name;
    node->as.variable.length = length;
    return node;
}

AstNode* astNewAssign(Arena* arena, const char* name, int length, AstNode* value, int line, int col) {
    AstNode* node = newNode(arena, NODE_ASSIGN, line, col);
    node->as.assign.name = name;
    node->as.assign.length = length;
    node->as.assign.value = value;
    return node;
}

AstNode* astNewCompoundAssign(Arena* arena, const char* name, int length, TokenType op, AstNode* value, int line, int col) {
    AstNode* node = newNode(arena, NODE_COMPOUND_ASSIGN, line, col);
    node->as.compoundAssign.name = name;
    node->as.compoundAssign.length = length;
    node->as.compoundAssign.op = op;
    node->as.compoundAssign.value = value;
    return node;
}

AstNode* astNewCall(Arena* arena, AstNode* callee, AstNode** args, int argCount, int line, int col) {
    AstNode* node = newNode(arena, NODE_CALL, line, col);
    node->as.call.callee = callee;
    node->as.call.args = args;
    node->as.call.argCount = argCount;
    return node;
}

AstNode* astNewIndex(Arena* arena, AstNode* object, AstNode* index, int line, int col) {
    AstNode* node = newNode(arena, NODE_INDEX, line, col);
    node->as.index.object = object;
    node->as.index.index = index;
    return node;
}

AstNode* astNewIndexSet(Arena* arena, AstNode* object, AstNode* index, AstNode* value, int line, int col) {
    AstNode* node = newNode(arena, NODE_INDEX_SET, line, col);
    node->as.indexSet.object = object;
    node->as.indexSet.index = index;
    node->as.indexSet.value = value;
    return node;
}

AstNode* astNewDot(Arena* arena, AstNode* object, const char* name, int nameLength, int line, int col) {
    AstNode* node = newNode(arena, NODE_DOT, line, col);
    node->as.dot.object = object;
    node->as.dot.name = name;
    node->as.dot.nameLength = nameLength;
    return node;
}

AstNode* astNewDotSet(Arena* arena, AstNode* object, const char* name, int nameLength, AstNode* value, int line, int col) {
    AstNode* node = newNode(arena, NODE_DOT_SET, line, col);
    node->as.dotSet.object = object;
    node->as.dotSet.name = name;
    node->as.dotSet.nameLength = nameLength;
    node->as.dotSet.value = value;
    return node;
}

AstNode* astNewList(Arena* arena, AstNode** elements, int count, int line, int col) {
    AstNode* node = newNode(arena, NODE_LIST, line, col);
    node->as.list.elements = elements;
    node->as.list.count = count;
    return node;
}

AstNode* astNewMap(Arena* arena, AstNode** keys, AstNode** values, int count, int line, int col) {
    AstNode* node = newNode(arena, NODE_MAP, line, col);
    node->as.map.keys = keys;
    node->as.map.values = values;
    node->as.map.count = count;
    return node;
}

AstNode* astNewLambda(Arena* arena, const char** params, int* paramLengths, int paramCount, AstNode* body, int line, int col) {
    AstNode* node = newNode(arena, NODE_LAMBDA, line, col);
    node->as.function.name = NULL;
    node->as.function.nameLength = 0;
    node->as.function.params = params;
    node->as.function.paramLengths = paramLengths;
    node->as.function.paramCount = paramCount;
    node->as.function.body = body;
    return node;
}

AstNode* astNewPipe(Arena* arena, AstNode* left, AstNode* right, int line, int col) {
    AstNode* node = newNode(arena, NODE_PIPE, line, col);
    node->as.pipe.left = left;
    node->as.pipe.right = right;
    return node;
}

AstNode* astNewRange(Arena* arena, AstNode* start, AstNode* end, int line, int col) {
    AstNode* node = newNode(arena, NODE_RANGE, line, col);
    node->as.range.start = start;
    node->as.range.end = end;
    return node;
}

AstNode* astNewExprStmt(Arena* arena, AstNode* expr, int line, int col) {
    AstNode* node = newNode(arena, NODE_EXPRESSION_STMT, line, col);
    node->as.exprStmt.expression = expr;
    return node;
}

AstNode* astNewBlock(Arena* arena, AstNode** stmts, int count, int line, int col) {
    AstNode* node = newNode(arena, NODE_BLOCK, line, col);
    node->as.block.statements = stmts;
    node->as.block.count = count;
    return node;
}

AstNode* astNewIf(Arena* arena, AstNode* condition, AstNode* thenBranch, AstNode* elseBranch, int line, int col) {
    AstNode* node = newNode(arena, NODE_IF, line, col);
    node->as.ifStmt.condition = condition;
    node->as.ifStmt.thenBranch = thenBranch;
    node->as.ifStmt.elseBranch = elseBranch;
    return node;
}

AstNode* astNewWhile(Arena* arena, AstNode* condition, AstNode* body, int line, int col) {
    AstNode* node = newNode(arena, NODE_WHILE, line, col);
    node->as.whileStmt.condition = condition;
    node->as.whileStmt.body = body;
    return node;
}

AstNode* astNewFor(Arena* arena, const char* varName, int varNameLength, AstNode* iterable, AstNode* body, int line, int col) {
    AstNode* node = newNode(arena, NODE_FOR, line, col);
    node->as.forStmt.varName = varName;
    node->as.forStmt.varNameLength = varNameLength;
    node->as.forStmt.iterable = iterable;
    node->as.forStmt.body = body;
    return node;
}

AstNode* astNewReturn(Arena* arena, AstNode* value, int line, int col) {
    AstNode* node = newNode(arena, NODE_RETURN, line, col);
    node->as.returnStmt.value = value;
    return node;
}

AstNode* astNewBreak(Arena* arena, int line, int col) {
    return newNode(arena, NODE_BREAK, line, col);
}

AstNode* astNewContinue(Arena* arena, int line, int col) {
    return newNode(arena, NODE_CONTINUE, line, col);
}

AstNode* astNewVarDecl(Arena* arena, const char* name, int length, AstNode* initializer, int line, int col) {
    AstNode* node = newNode(arena, NODE_VAR_DECL, line, col);
    node->as.varDecl.name = name;
    node->as.varDecl.length = length;
    node->as.varDecl.initializer = initializer;
    return node;
}

AstNode* astNewFnDecl(Arena* arena, const char* name, int nameLength, const char** params, int* paramLengths, int paramCount, AstNode* body, int line, int col) {
    AstNode* node = newNode(arena, NODE_FN_DECL, line, col);
    node->as.function.name = name;
    node->as.function.nameLength = nameLength;
    node->as.function.params = params;
    node->as.function.paramLengths = paramLengths;
    node->as.function.paramCount = paramCount;
    node->as.function.body = body;
    return node;
}

AstNode* astNewAllow(Arena* arena, TokenType permType, const char* target, int targetLength, int line, int col) {
    AstNode* node = newNode(arena, NODE_ALLOW, line, col);
    node->as.allow.permType = permType;
    node->as.allow.target = target;
    node->as.allow.targetLength = targetLength;
    return node;
}

AstNode* astNewParallel(Arena* arena, AstNode** tasks, int taskCount, int line, int col) {
    AstNode* node = newNode(arena, NODE_PARALLEL, line, col);
    node->as.parallel.tasks = tasks;
    node->as.parallel.taskCount = taskCount;
    return node;
}

AstNode* astNewOnFailure(Arena* arena, AstNode* body, int line, int col) {
    AstNode* node = newNode(arena, NODE_ON_FAILURE, line, col);
    node->as.onFailure.body = body;
    return node;
}

AstNode* astNewExec(Arena* arena, AstNode* command, AstNode** args, int argCount, int line, int col) {
    AstNode* node = newNode(arena, NODE_EXEC, line, col);
    node->as.exec.command = command;
    node->as.exec.args = args;
    node->as.exec.argCount = argCount;
    return node;
}

AstNode* astNewImport(Arena* arena, const char* path, int pathLength, const char* alias, int aliasLength, int line, int col) {
    AstNode* node = newNode(arena, NODE_IMPORT, line, col);
    node->as.import.path = path;
    node->as.import.pathLength = pathLength;
    node->as.import.alias = alias;
    node->as.import.aliasLength = aliasLength;
    return node;
}

AstNode* astNewMatch(Arena* arena, AstNode* subject, AstNode** arms, int armCount, int line, int col) {
    AstNode* node = newNode(arena, NODE_MATCH, line, col);
    node->as.match.subject = subject;
    node->as.match.arms = arms;
    node->as.match.armCount = armCount;
    return node;
}

AstNode* astNewMatchArm(Arena* arena, AstNode* pattern, AstNode* body, int line, int col) {
    AstNode* node = newNode(arena, NODE_MATCH_ARM, line, col);
    node->as.matchArm.pattern = pattern;
    node->as.matchArm.body = body;
    return node;
}

AstNode* astNewProgram(Arena* arena, AstNode** stmts, int count, int line, int col) {
    AstNode* node = newNode(arena, NODE_PROGRAM, line, col);
    node->as.block.statements = stmts;
    node->as.block.count = count;
    return node;
}

// ---- Debug Printing ----

static void printIndent(int indent) {
    for (int i = 0; i < indent; i++) printf("  ");
}

void astPrint(AstNode* node, int indent) {
    if (node == NULL) {
        printIndent(indent);
        printf("(null)\n");
        return;
    }

    printIndent(indent);

    switch (node->type) {
        case NODE_LITERAL:
            switch (node->as.literal.value.type) {
                case LIT_NUMBER:
                    printf("Literal(%.14g)\n", node->as.literal.value.as.number);
                    break;
                case LIT_STRING:
                    printf("Literal(\"%.*s\")\n",
                           node->as.literal.value.as.string.length,
                           node->as.literal.value.as.string.chars);
                    break;
                case LIT_BOOL:
                    printf("Literal(%s)\n", node->as.literal.value.as.boolean ? "true" : "false");
                    break;
                case LIT_NIL:
                    printf("Literal(nil)\n");
                    break;
            }
            break;

        case NODE_UNARY:
            printf("Unary(%s)\n", tokenTypeName(node->as.unary.op));
            astPrint(node->as.unary.operand, indent + 1);
            break;

        case NODE_BINARY:
            printf("Binary(%s)\n", tokenTypeName(node->as.binary.op));
            astPrint(node->as.binary.left, indent + 1);
            astPrint(node->as.binary.right, indent + 1);
            break;

        case NODE_VARIABLE:
            printf("Variable(%.*s)\n", node->as.variable.length, node->as.variable.name);
            break;

        case NODE_ASSIGN:
            printf("Assign(%.*s)\n", node->as.assign.length, node->as.assign.name);
            astPrint(node->as.assign.value, indent + 1);
            break;

        case NODE_COMPOUND_ASSIGN:
            printf("CompoundAssign(%.*s %s)\n",
                   node->as.compoundAssign.length, node->as.compoundAssign.name,
                   tokenTypeName(node->as.compoundAssign.op));
            astPrint(node->as.compoundAssign.value, indent + 1);
            break;

        case NODE_CALL:
            printf("Call\n");
            printIndent(indent + 1); printf("callee:\n");
            astPrint(node->as.call.callee, indent + 2);
            for (int i = 0; i < node->as.call.argCount; i++) {
                printIndent(indent + 1); printf("arg %d:\n", i);
                astPrint(node->as.call.args[i], indent + 2);
            }
            break;

        case NODE_INDEX:
            printf("Index\n");
            astPrint(node->as.index.object, indent + 1);
            astPrint(node->as.index.index, indent + 1);
            break;

        case NODE_INDEX_SET:
            printf("IndexSet\n");
            astPrint(node->as.indexSet.object, indent + 1);
            astPrint(node->as.indexSet.index, indent + 1);
            astPrint(node->as.indexSet.value, indent + 1);
            break;

        case NODE_DOT:
            printf("Dot(.%.*s)\n", node->as.dot.nameLength, node->as.dot.name);
            astPrint(node->as.dot.object, indent + 1);
            break;

        case NODE_DOT_SET:
            printf("DotSet(.%.*s)\n", node->as.dotSet.nameLength, node->as.dotSet.name);
            astPrint(node->as.dotSet.object, indent + 1);
            astPrint(node->as.dotSet.value, indent + 1);
            break;

        case NODE_LIST:
            printf("List(%d elements)\n", node->as.list.count);
            for (int i = 0; i < node->as.list.count; i++) {
                astPrint(node->as.list.elements[i], indent + 1);
            }
            break;

        case NODE_MAP:
            printf("Map(%d entries)\n", node->as.map.count);
            for (int i = 0; i < node->as.map.count; i++) {
                printIndent(indent + 1); printf("key:\n");
                astPrint(node->as.map.keys[i], indent + 2);
                printIndent(indent + 1); printf("value:\n");
                astPrint(node->as.map.values[i], indent + 2);
            }
            break;

        case NODE_LAMBDA:
            printf("Lambda(");
            for (int i = 0; i < node->as.function.paramCount; i++) {
                if (i > 0) printf(", ");
                printf("%.*s", node->as.function.paramLengths[i], node->as.function.params[i]);
            }
            printf(")\n");
            astPrint(node->as.function.body, indent + 1);
            break;

        case NODE_PIPE:
            printf("Pipe\n");
            astPrint(node->as.pipe.left, indent + 1);
            astPrint(node->as.pipe.right, indent + 1);
            break;

        case NODE_RANGE:
            printf("Range\n");
            astPrint(node->as.range.start, indent + 1);
            astPrint(node->as.range.end, indent + 1);
            break;

        case NODE_EXPRESSION_STMT:
            printf("ExprStmt\n");
            astPrint(node->as.exprStmt.expression, indent + 1);
            break;

        case NODE_BLOCK:
            printf("Block(%d stmts)\n", node->as.block.count);
            for (int i = 0; i < node->as.block.count; i++) {
                astPrint(node->as.block.statements[i], indent + 1);
            }
            break;

        case NODE_IF:
            printf("If\n");
            printIndent(indent + 1); printf("condition:\n");
            astPrint(node->as.ifStmt.condition, indent + 2);
            printIndent(indent + 1); printf("then:\n");
            astPrint(node->as.ifStmt.thenBranch, indent + 2);
            if (node->as.ifStmt.elseBranch) {
                printIndent(indent + 1); printf("else:\n");
                astPrint(node->as.ifStmt.elseBranch, indent + 2);
            }
            break;

        case NODE_WHILE:
            printf("While\n");
            printIndent(indent + 1); printf("condition:\n");
            astPrint(node->as.whileStmt.condition, indent + 2);
            printIndent(indent + 1); printf("body:\n");
            astPrint(node->as.whileStmt.body, indent + 2);
            break;

        case NODE_FOR:
            printf("For(%.*s)\n", node->as.forStmt.varNameLength, node->as.forStmt.varName);
            printIndent(indent + 1); printf("iterable:\n");
            astPrint(node->as.forStmt.iterable, indent + 2);
            printIndent(indent + 1); printf("body:\n");
            astPrint(node->as.forStmt.body, indent + 2);
            break;

        case NODE_RETURN:
            printf("Return\n");
            if (node->as.returnStmt.value) {
                astPrint(node->as.returnStmt.value, indent + 1);
            }
            break;

        case NODE_BREAK:
            printf("Break\n");
            break;

        case NODE_CONTINUE:
            printf("Continue\n");
            break;

        case NODE_VAR_DECL:
            printf("VarDecl(%.*s)\n", node->as.varDecl.length, node->as.varDecl.name);
            astPrint(node->as.varDecl.initializer, indent + 1);
            break;

        case NODE_FN_DECL:
            printf("FnDecl(%.*s, ", node->as.function.nameLength, node->as.function.name);
            for (int i = 0; i < node->as.function.paramCount; i++) {
                if (i > 0) printf(", ");
                printf("%.*s", node->as.function.paramLengths[i], node->as.function.params[i]);
            }
            printf(")\n");
            astPrint(node->as.function.body, indent + 1);
            break;

        case NODE_ALLOW:
            printf("Allow(%s, \"%.*s\")\n",
                   tokenTypeName(node->as.allow.permType),
                   node->as.allow.targetLength, node->as.allow.target);
            break;

        case NODE_PARALLEL:
            printf("Parallel(%d tasks)\n", node->as.parallel.taskCount);
            for (int i = 0; i < node->as.parallel.taskCount; i++) {
                astPrint(node->as.parallel.tasks[i], indent + 1);
            }
            break;

        case NODE_ON_FAILURE:
            printf("OnFailure\n");
            astPrint(node->as.onFailure.body, indent + 1);
            break;

        case NODE_EXEC:
            printf("Exec\n");
            printIndent(indent + 1); printf("command:\n");
            astPrint(node->as.exec.command, indent + 2);
            for (int i = 0; i < node->as.exec.argCount; i++) {
                printIndent(indent + 1); printf("arg %d:\n", i);
                astPrint(node->as.exec.args[i], indent + 2);
            }
            break;

        case NODE_IMPORT:
            printf("Import(\"%.*s\"", node->as.import.pathLength, node->as.import.path);
            if (node->as.import.alias) {
                printf(" as %.*s", node->as.import.aliasLength, node->as.import.alias);
            }
            printf(")\n");
            break;

        case NODE_MATCH:
            printf("Match\n");
            printIndent(indent + 1); printf("subject:\n");
            astPrint(node->as.match.subject, indent + 2);
            for (int i = 0; i < node->as.match.armCount; i++) {
                astPrint(node->as.match.arms[i], indent + 1);
            }
            break;

        case NODE_MATCH_ARM:
            printf("MatchArm\n");
            printIndent(indent + 1); printf("pattern:\n");
            astPrint(node->as.matchArm.pattern, indent + 2);
            printIndent(indent + 1); printf("body:\n");
            astPrint(node->as.matchArm.body, indent + 2);
            break;

        case NODE_PROGRAM:
            printf("Program(%d stmts)\n", node->as.block.count);
            for (int i = 0; i < node->as.block.count; i++) {
                astPrint(node->as.block.statements[i], indent + 1);
            }
            break;
    }
}
