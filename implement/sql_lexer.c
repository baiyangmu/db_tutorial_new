#include "sql_lexer.h"

#include <ctype.h>
#include <string.h>
#include <stdlib.h>

static int is_ident_char(char c){
    return isalnum((unsigned char)c) || c == '_' || c == '.';
}

void lexer_init(Lexer* lx,const char* s){
    lx->input = s;
    lx->pos = 0;
    lx->cur.type = TOK_ILLEGAL;
    lx->cur.text[0] = '\0';
}


void lexer_next(Lexer* lx){
    const char* s = lx->input;
    size_t i = lx->pos;

    while (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r') {
        i++;
    }

    char c = s[i];

    if (c == '\0'){
        lx->cur.type = TOK_EOF;
        lx->cur.text[0] = '\0';
        lx->pos = i;
        return;
    }

    if(c == ','){
        lx->cur.type = TOK_COMMA;
        strcpy(lx->cur.text,",");
        lx->pos = i + 1;
        return;
    }

    if (c == '*'){
        lx->cur.type = TOK_STAR;
        strcpy(lx->cur.text, "*");
        lx->pos = i + 1;
        return;
    }


    if (c == '(') {
        lx->cur.type = TOK_LPAREN;
        strcpy(lx->cur.text, "(");
        lx->pos = i + 1;
        return;
    }

    if (c == ')') {
        lx->cur.type = TOK_RPAREN;
        strcpy(lx->cur.text, ")");
        lx->pos = i + 1;
        return;
    }

    if (c == '=') {
        lx->cur.type = TOK_EQ;
        strcpy(lx->cur.text, "=");
        lx->pos = i + 1;
        return;
    }

    if (c == '!' && s[i + 1] == '=') {
        lx->cur.type = TOK_NEQ;
        strcpy(lx->cur.text, "!=");
        lx->pos = i + 2;
        return;
    }

    if (c == '<') {
        if (s[i + 1] == '=') {
            lx->cur.type = TOK_LTE;
            strcpy(lx->cur.text, "<=");
            lx->pos = i + 2;
        } else {
            lx->cur.type = TOK_LT;
            strcpy(lx->cur.text, "<");
            lx->pos = i + 1;
        }
        return;
    }

    if (c == '>') {
        if (s[i + 1] == '=') {
            lx->cur.type = TOK_GTE;
            strcpy(lx->cur.text, ">=");
            lx->pos = i + 2;
        } else {
            lx->cur.type = TOK_GT;
            strcpy(lx->cur.text, ">");
            lx->pos = i + 1;
        }
        return;
    }

    if(isdigit((unsigned char)c) || (c == '-' && isdigit((unsigned char)s[i + 1]))){
        size_t j = i + 1;
        while(isdigit((unsigned char)s[j])){
            j++;
        }
        size_t len = j - i;
        if(len >= sizeof(lx->cur.text)){
            len = sizeof(lx->cur.text) - 1;
        }
        strncpy(lx->cur.text,s+i,len);
        lx->cur.text[len] = '\0';
        lx->cur.type = TOK_NUMBER;
        lx->pos = j;
        return;
    }


    if(c == '\'' || c == '\"'){
        char q = c;
        size_t j = i + 1;
        while(s[j] != '\0' && s[j] != q){
            j++;
        }
        size_t len = (j > i + 1)? (j - (i + 1)) : 0;
        if (len >= sizeof(lx->cur.text)){
            len = sizeof(lx->cur.text) - 1;
        }
        strncpy(lx->cur.text,s+i+1,len);
        lx->cur.text[len] = '\0';
        lx->cur.type = TOK_STRING;
        lx->pos = (s[j] == q) ? j + 1 : j;
        return;
    }


    if(is_ident_char(c)){
        size_t j = i+1;
        while(is_ident_char(s[j])){
            j++;
        }

        size_t len = j - i;
        if(len >= sizeof(lx->cur.text)){
            len = sizeof(lx->cur.text) - 1;
        }
        strncpy(lx->cur.text,s+i,len);
        lx->cur.text[len] = '\0';

        char tmp[256];
        size_t k;
        for (k = 0 ; k < len && k < sizeof(tmp) - 1 ; k++){
            tmp[k] = (char) toupper((unsigned char)lx->cur.text[k]);
        }
        tmp[k] = '\0';

        if (strcmp(tmp, "SELECT") == 0) {
            lx->cur.type = TOK_SELECT;
        } else if (strcmp(tmp, "FROM") == 0) {
            lx->cur.type = TOK_FROM;
        } else if (strcmp(tmp, "WHERE") == 0) {
            lx->cur.type = TOK_WHERE;
        } else if (strcmp(tmp, "INSERT") == 0) {
            lx->cur.type = TOK_INSERT;
        } else if (strcmp(tmp, "INTO") == 0) {
            lx->cur.type = TOK_INTO;
        } else if (strcmp(tmp, "UPDATE") == 0) {
            lx->cur.type = TOK_UPDATE;
        } else if (strcmp(tmp, "SET") == 0) {
            lx->cur.type = TOK_SET;
        } else if (strcmp(tmp, "DELETE") == 0) {
            lx->cur.type = TOK_DELETE;
        } else if (strcmp(tmp, "IN") == 0) {
            lx->cur.type = TOK_IN;
        } else if (strcmp(tmp, "BETWEEN") == 0) {
            lx->cur.type = TOK_BETWEEN;
        } else if (strcmp(tmp, "AND") == 0) {
            lx->cur.type = TOK_AND;
        } else if (strcmp(tmp, "OR") == 0) {
            lx->cur.type = TOK_OR;
        } else if (strcmp(tmp, "NOT") == 0) {
            lx->cur.type = TOK_NOT;
        } else if (strcmp(tmp, "IS") == 0) {
            lx->cur.type = TOK_IS;
        } else if (strcmp(tmp, "NULL") == 0) {
            lx->cur.type = TOK_NULL;
        } else if (strcmp(tmp, "AS") == 0) {
            lx->cur.type = TOK_AS;
        } else if (strcmp(tmp, "CREATE") == 0) {
            lx->cur.type = TOK_CREATE;
        } else if (strcmp(tmp, "TABLE") == 0) {
            lx->cur.type = TOK_TABLE;
        } else if (strcmp(tmp, "USE") == 0) {
            lx->cur.type = TOK_USE;
        } else {
            lx->cur.type = TOK_IDENT;
        }

        lx->pos = j;
        return;

    }

    lx->cur.type = TOK_ILLEGAL;
    lx->cur.text[0] = c;
    lx->cur.text[1] = '\0';
    lx->pos = i + 1;
        
    return;
}