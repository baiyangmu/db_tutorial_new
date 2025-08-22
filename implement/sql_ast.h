#ifndef SQL_AST_H
#define SQL_AST_H

#include <stdint.h>

typedef enum {
    EXPR_COLUMN,
    EXPR_LITERAL,
    EXPR_BINARY,
    EXPR_UNARY,
    EXPR_IN,
    EXPR_BETWEEN,
    EXPR_ISNULL
} ExprKind;


typedef struct Expr Expr;


struct Expr {
    ExprKind kind;

    char text[256];

    char op[16];
    Expr* left;
    Expr* right;

    Expr** items;
    uint32_t n_items;

};


Expr* expr_new(void);
void expr_free(Expr* e);


#endif