#include "sql_parser.h"
#include "sql_lexer.h"
#include "sql_ast.h"

#include <stdlib.h>
#include <string.h>


typedef struct{
    Lexer lx;
} Parser;


static void parser_init(Parser* p,const char* s){
    lexer_init(&p->lx,s);
    lexer_next(&p->lx);
}

static void lexer_consume(Lexer* lx){
    lexer_next(lx);
}


static int accept(Lexer* lx,TokenType t){
    if(lx->cur.type == t){
        lexer_next(lx);
        return 1;
    }
    return 0;
}


Expr* expr_new(void){
    Expr* e = (Expr*) malloc(sizeof(Expr));
    if(!e){
        return NULL;
    }
    memset(e,0,sizeof(Expr));
    e->left = NULL;
    e->right = NULL;
    e->items = NULL;
    e->n_items = 0;
    return e;
}


void expr_free(Expr* e){
    if(!e){
        return;
    }
    if(e->left){
        expr_free(e->left);
    }
    if(e->right){
        expr_free(e->right);
    }
    if(e->items){
        for(uint32_t i = 0 ; i < e->n_items; i++){
            expr_free(e->items[i]);
        }
        free(e->items);
    }
    free(e);
}




static Expr* parse_primary(Parser* p);
static Expr* parse_unary(Parser* p);
static Expr* parse_comparison(Parser* p);
static Expr* parse_and(Parser* p);
static Expr* parse_expr(Parser* p);


static Expr* parse_primary(Parser* p){
    Lexer* lx = &p->lx;

    if(lx->cur.type == TOK_IDENT){
        Expr* e = expr_new();
        e->kind= EXPR_COLUMN;
        strncpy(e->text,lx->cur.text,sizeof(e->text)-1);
        lexer_next(lx);
        return e;
    }

    if(lx->cur.type == TOK_NUMBER || lx->cur.type == TOK_STRING){
        Expr* e = expr_new();
        e->kind = EXPR_LITERAL;
        strncpy(e->text,lx->cur.text,sizeof(e->text) - 1);
        lexer_next(lx);
        return e;
    }

    if(accept(lx,TOK_LPAREN)){
        Expr* inner = parse_expr(p);
        accept(lx,TOK_RPAREN);
        return inner;
    }

    return NULL;
}


static Expr* parse_unary(Parser* p){
    Lexer* lx = &p->lx;
    if(accept(lx,TOK_NOT)){
        Expr* operand = parse_unary(p);
        Expr* e = expr_new();
        e->kind = EXPR_UNARY;
        strncpy(e->op,"NOT",sizeof(e->op)-1);
        e->left = operand;
        return e;
    }
    return parse_primary(p);
}


static Expr* parse_comparison(Parser* p){
   Lexer* lx = &p->lx;
   Expr* left = parse_unary(p);
   if(!left){
    return NULL;
   } 
   if (lx->cur.type == TOK_EQ ||
    lx->cur.type == TOK_NEQ ||
    lx->cur.type == TOK_LT ||
    lx->cur.type == TOK_LTE ||
    lx->cur.type == TOK_GT ||
    lx->cur.type == TOK_GTE) {
        Expr* e = expr_new();
        e->kind = EXPR_BINARY;
        strncpy(e->op,lx->cur.text,sizeof(e->op)-1);
        e->left = left;
        lexer_next(lx);
        e->right = parse_unary(p);
        return e;
    }

    if(lx->cur.type == TOK_IS){
        lexer_next(lx);
        int is_not = 0;
        if(accept(lx,TOK_NOT)){
            is_not = 1;
        }
        if(accept(lx,TOK_NULL)){
            Expr* e = expr_new();
            e->kind = EXPR_ISNULL;
            e->left = left;
            if(is_not){
                strncpy(e->op,"IS NOT",sizeof(e->op) - 1);
            }else{
                strncpy(e->op,"IS",sizeof(e->op)-1);
            }
            return e;
        }
    }


    if(lx->cur.type == TOK_BETWEEN){
        lexer_next(lx);
        Expr* low = parse_primary(p);
        accept(lx,TOK_AND);
        Expr* high = parse_primary(p);
        Expr* e = expr_new();
        e->kind = EXPR_BETWEEN;
        e->left = left;
        /* pack low/high into right subtree to keep AST binary */
        e->right = expr_new();
        e->right->left = low;
        e->right->right = high;
        return e;
    }


    if(lx->cur.type == TOK_IN){
        lexer_next(lx);
        if(accept(lx,TOK_LPAREN)){
            Expr* e = expr_new();
            e->kind = EXPR_IN;
            e->left = left;
            Expr** items = (Expr**) malloc(sizeof(Expr*)*16);
            uint32_t n = 0;
            while(1){
                Expr* it = parse_primary(p);
                if(!it){
                    break;
                }
                items[n++] = it;
                if(accept(lx,TOK_COMMA)){
                    continue;
                }
                break;
            }
            accept(lx,TOK_RPAREN);
            e->items = items;
            e->n_items = n;
            return e;
        }
    }
    return left;
}

static Expr* parse_and(Parser* p){
    Expr* left = parse_comparison(p);
    Lexer* lx = &p->lx;
    while(lx->cur.type == TOK_AND){
        lexer_next(lx);
        Expr* right = parse_comparison(p);
        Expr* e = expr_new();
        e->kind = EXPR_BINARY;
        strncpy(e->op,"AND",sizeof(e->op) - 1);
        e->left = left;
        e->right = right;
        left = e;
    }
    return left;
}


static Expr* parse_expr(Parser* p){
    Expr* left = parse_and(p);
    Lexer* lx = &p->lx;
    while(lx->cur.type == TOK_OR){
        lexer_next(lx);
        Expr* right = parse_and(p);
        Expr* e = expr_new();
        e->kind = EXPR_BINARY;
        strncpy(e->op,"OR",sizeof(e->op) - 1);
        e->left = left;
        e->right = right;
        left = e;
    }
    return left;
}



static int parse_select(Parser* p,ParsedStmt* out){
    Lexer* lx = &p->lx;
    lexer_next(lx);

    out->proj_count = 0;
    out->select_all = 0;
    out->where = NULL;
    out->table_name[0] = '\0';

    if(lx->cur.type == TOK_STAR){
        out->select_all = 1;
        lexer_next(lx);
    }else{
        while(lx->cur.type == TOK_IDENT){
            if(out->proj_count < PARSED_MAX_PROJ){
                strncpy(out->proj_list[out->proj_count], lx->cur.text, PARSED_MAX_PROJ_NAME_LEN - 1);
                out->proj_list[out->proj_count][PARSED_MAX_PROJ_NAME_LEN - 1] = '\0';
                out->proj_count++;
            }
            lexer_next(lx);
            if(!accept(lx,TOK_COMMA)){
                break;
            }
        }
    }



    if(!accept(lx,TOK_FROM)){
        return -1;
    }

    if(lx->cur.type != TOK_IDENT){
        return -1;
    }

    strncpy(out->table_name, lx->cur.text, PARSED_TABLE_NAME_LEN - 1);
    out->table_name[PARSED_TABLE_NAME_LEN - 1] = '\0';
    lexer_next(lx);

    if(accept(lx,TOK_WHERE)){
        out->where = parse_expr(p);
    }

    if(accept(lx,TOK_ORDER)){
        if(!accept(lx,TOK_BY)){
            return -1;
        }
        if(lx->cur.type != TOK_IDENT){
            return -1;
        }
        strncpy(out->order_by,lx->cur.text,PARSED_MAX_PROJ_NAME_LEN-1);
        out->order_by[PARSED_MAX_PROJ_NAME_LEN-1] = '\0';
        lexer_next(lx);
        out->order_desc = 0;
        if(accept(lx,TOK_DESC)){
            out->order_desc = 1;
        } else {
            accept(lx,TOK_ASC);
        }
    }


    if(accept(lx,TOK_LIMIT)){
        if(lx->cur.type != TOK_NUMBER){
            return -1;
        }
        out->limit = (uint32_t)atoi(lx->cur.text);
        out->has_limit = 1;
        lexer_next(lx);
        if(accept(lx,TOK_OFFSET)){
            if(lx->cur.type != TOK_NUMBER){
                return -1;
            }
            out->offset = (uint32_t)atoi(lx->cur.text);
            out->has_offset = 1;
            lexer_next(lx);
        } else {
            out->has_offset = 0;
        }
    } else {
        out->has_limit = 0;
        out->has_offset = 0;
    }

    return 0;
}



void parsed_stmt_free(ParsedStmt* ps){
    if(!ps){
        return;
    }
    if(ps->where){
        expr_free(ps->where);
        ps->where = NULL;
    }
    if(ps->insert_value){
        for(uint32_t i = 0 ; i < ps->insert_n ; i++){
            free(ps->insert_value[i]);
        }
        free(ps->insert_value);
        ps->insert_value = NULL;
        ps->insert_n = 0;
    }
}

int parse_sql_to_parsed_stmt(const char* sql, ParsedStmt* out) {
    if (!out || !sql) return -1;
    /* Ensure out is zeroed so parsed_stmt_free can safely check pointers */
    memset(out, 0, sizeof(*out));
    Parser p;
    parser_init(&p, sql);

    /* Only implement SELECT for now */
    if (p.lx.cur.type == TOK_SELECT) {
        int ret = parse_select(&p, out);
        if (ret != 0) return -1;
        out->kind = PARSED_SELECT;
        return 0;
    }

    return -1;
}