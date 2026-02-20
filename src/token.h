#ifndef glipt_token_h
#define glipt_token_h

typedef enum {
    // Single-character tokens
    TOKEN_LEFT_PAREN,       // (
    TOKEN_RIGHT_PAREN,      // )
    TOKEN_LEFT_BRACE,       // {
    TOKEN_RIGHT_BRACE,      // }
    TOKEN_LEFT_BRACKET,     // [
    TOKEN_RIGHT_BRACKET,    // ]
    TOKEN_COMMA,            // ,
    TOKEN_DOT,              // .
    TOKEN_COLON,            // :
    TOKEN_SEMICOLON,        // ;
    TOKEN_PLUS,             // +
    TOKEN_MINUS,            // -
    TOKEN_STAR,             // *
    TOKEN_SLASH,            // /
    TOKEN_PERCENT,          // %

    // One or two character tokens
    TOKEN_BANG,             // !
    TOKEN_BANG_EQUAL,       // !=
    TOKEN_EQUAL,            // =
    TOKEN_EQUAL_EQUAL,      // ==
    TOKEN_GREATER,          // >
    TOKEN_GREATER_EQUAL,    // >=
    TOKEN_LESS,             // <
    TOKEN_LESS_EQUAL,       // <=
    TOKEN_ARROW,            // ->
    TOKEN_PIPE,             // |
    TOKEN_PIPE_PIPE,        // ||
    TOKEN_AMP,              // &
    TOKEN_AMP_AMP,          // &&
    TOKEN_PLUS_EQUAL,       // +=
    TOKEN_MINUS_EQUAL,      // -=
    TOKEN_STAR_EQUAL,       // *=
    TOKEN_SLASH_EQUAL,      // /=
    TOKEN_DOT_DOT,         // ..

    // Literals
    TOKEN_IDENTIFIER,
    TOKEN_STRING,           // "..." or '...'
    TOKEN_NUMBER,           // 42, 3.14
    TOKEN_RAW_STRING,       // `...`
    TOKEN_FSTRING,          // f"...{expr}..."

    // Keywords
    TOKEN_ALLOW,
    TOKEN_AND,
    TOKEN_AS,
    TOKEN_BREAK,
    TOKEN_CONTINUE,
    TOKEN_ELSE,
    TOKEN_EXEC,
    TOKEN_EXIT,
    TOKEN_FAILURE,
    TOKEN_FALSE,
    TOKEN_FN,
    TOKEN_FOR,
    TOKEN_IF,
    TOKEN_IMPORT,
    TOKEN_IN,
    TOKEN_LET,
    TOKEN_MATCH,
    TOKEN_NET,
    TOKEN_NIL,
    TOKEN_NOT,
    TOKEN_ON,
    TOKEN_OR,
    TOKEN_PARALLEL,
    TOKEN_READ,
    TOKEN_RETURN,
    TOKEN_TRUE,
    TOKEN_WHILE,
    TOKEN_WRITE,
    TOKEN_ENV,

    // Special
    TOKEN_NEWLINE,
    TOKEN_ERROR,
    TOKEN_EOF,
} TokenType;

typedef struct {
    TokenType type;
    const char* start;  // pointer into source
    int length;
    int line;
    int column;
} Token;

const char* tokenTypeName(TokenType type);

#endif
