#pragma once
#include <stddef.h>
#include <stdio.h>
#include "sv.h"

typedef struct {
  String_View filename;
  size_t line; // 0-based
  size_t col; // 0-based
} Location;

typedef enum {
  TK_NAME,
  TK_DIRECTIVE,
  TK_COMMENT,
  TK_TYPEDF,
  TK_STRUCT,
  TK_ENUM,
  TK_PAREN,
  TK_SEP,
  TK_OP,
  TK_ACCESS,
  TK_LIT,
  TK_ATTRIB,
} TokenKind;
typedef struct {
  Location loc;
  String_View content;
  TokenKind kind;
} Token;
typedef struct {
  bool has_value;
  Token token;
} TokenOrEnd;

typedef struct {
  Location loc;
  String_View content;
  TokenOrEnd peek;
} Lexer;

Lexer lexer_create(String_View filename, String_View content);
TokenOrEnd lexer_peek_token(Lexer*);
TokenOrEnd lexer_get_token(Lexer*);
Token lexer_expect_token(Lexer*);
void lexer_dump_loc(Location, FILE*);
__attribute__((format(printf,3,4))) void lexer_dump_err(Location, FILE*, char *fmt, ...);
#define lexer_exit_err(...) { lexer_dump_err(__VA_ARGS__); exit(1); } while(0)
__attribute__((format(printf,3,4))) void lexer_dump_warn(Location, FILE*, char *fmt, ...);
void lexer_dump_token(Token, FILE*);
