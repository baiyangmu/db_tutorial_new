#ifndef SQL_LEXER_H
#define SQL_LEXER_H

#include <stddef.h>
typedef enum{
    TOK_ILLEGAL,
    TOK_EOF,
    TOK_IDENT,
    TOK_NUMBER,
    TOK_STRING,
    TOK_COMMA,
    TOK_STAR,
    TOK_LPAREN,
    TOK_RPAREN,
    TOK_EQ,
    TOK_NEQ,
    TOK_LT,
    TOK_LTE,
    TOK_GT,
    TOK_GTE,
    TOK_AND,
    TOK_OR,
    TOK_NOT,
    TOK_IN,
    TOK_BETWEEN,
    TOK_IS,
    TOK_NULL,
    TOK_SELECT,
    TOK_FROM,
    TOK_WHERE,
    TOK_INSERT,
    TOK_INTO,
    TOK_UPDATE,
    TOK_SET,
    TOK_DELETE,
    TOK_CREATE,
    TOK_TABLE,
    TOK_USE,
    TOK_ORDER,
    TOK_BY,
    TOK_LIMIT,
    TOK_OFFSET,
    TOK_ASC,
    TOK_DESC,
    TOK_AS
} TokenType;

typedef struct{
    TokenType type;
    char text[256];
} Token;


typedef struct{
    const char* input;
    size_t pos;
    Token cur;
} Lexer;

void lexer_init(Lexer* lx,const char* s);
void lexer_next(Lexer* lx);


#endif