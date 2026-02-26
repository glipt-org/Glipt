#ifndef glipt_opcode_h
#define glipt_opcode_h

typedef enum {
    // Constants & Literals
    OP_CONSTANT,        // 1-byte index into constant pool
    OP_NIL,
    OP_TRUE,
    OP_FALSE,

    // Arithmetic
    OP_ADD,
    OP_SUBTRACT,
    OP_MULTIPLY,
    OP_DIVIDE,
    OP_MODULO,
    OP_NEGATE,

    // Comparison
    OP_EQUAL,
    OP_NOT_EQUAL,
    OP_GREATER,
    OP_GREATER_EQUAL,
    OP_LESS,
    OP_LESS_EQUAL,

    // Logic
    OP_NOT,

    // Variables
    OP_GET_LOCAL,       // 1-byte slot index
    OP_SET_LOCAL,
    OP_GET_GLOBAL,      // 1-byte constant index (name)
    OP_SET_GLOBAL,
    OP_DEFINE_GLOBAL,
    OP_GET_UPVALUE,
    OP_SET_UPVALUE,

    // Control Flow
    OP_JUMP,            // 2-byte offset (unconditional)
    OP_JUMP_IF_FALSE,   // 2-byte offset (conditional)
    OP_LOOP,            // 2-byte offset (jump backward)

    // Functions
    OP_CALL,            // 1-byte arg count
    OP_CLOSURE,         // 1-byte constant index + upvalue descriptors
    OP_RETURN,
    OP_CLOSE_UPVALUE,

    // Data Structures
    OP_BUILD_LIST,      // 1-byte element count
    OP_BUILD_MAP,       // 1-byte entry count (pairs)
    OP_INDEX_GET,
    OP_INDEX_SET,
    OP_GET_PROPERTY,    // 1-byte constant index (property name)
    OP_SET_PROPERTY,    // 1-byte constant index (property name)

    // Utility
    OP_PRINT,
    OP_POP,

    // Glipt-specific
    OP_ALLOW,           // 1-byte perm type + 1-byte constant index (target string)
    OP_PUSH_HANDLER,    // 2-byte jump offset to handler code
    OP_POP_HANDLER,
    OP_THROW,
    OP_IMPORT,          // 1-byte path constant + 1-byte name constant
} OpCode;

#endif
