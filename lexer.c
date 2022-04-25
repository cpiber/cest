#include <assert.h>
#include <stdio.h>
#include <stdarg.h>
#include "lexer.h"

#define SV_PEEK(sv, i, name, body) {                              \
    if ((sv).count > i) { const char name = (sv).data[i]; body; } \
  } while(0)
#define PRODUCE(kind) {                \
    last_kind = kind;                  \
    lexer_consume_char(lexer, &token); \
  } while(0)


Lexer lexer_create(String_View filename, String_View content) {
  return (Lexer) {
    .content = content,
    .loc = { .filename = filename },
  };
}


static bool is_space_not_newline(char c) {
  return isspace(c) && c != '\n';
}

static bool is_ident(char c) {
  return isalnum(c) || c == '_';
}

// NOTE: these do not check for correctness of number literals (e.g. 10', 10''0, 1..4, 10LF, ...)
static bool is_number(char c) {
  return isdigit(c) || c == '\'' || c == '.';
}
static bool is_hex_number(char c) {
  const char u = toupper(c);
  return isdigit(c) || (u >= 'A' && u <= 'F') || c == '\'';
}
static bool is_bin_number(char c) {
  return c == '0' || c == '1' || c == '\'';
}
static bool is_oct_number(char c) {
  return (isdigit(c) && c != '9' && c != '8') || c == '\'';
}
static bool is_number_suffix(char c) {
  const char u = toupper(c);
  return u == 'L' || u == 'U' || u == 'F';
}

static void lexer_remove_space(Lexer *lexer) {
  while (true) {
    const String_View space = sv_chop_left_while(&lexer->content, is_space_not_newline);
    lexer->loc.col += space.count;

    if (lexer->content.count == 0 || lexer->content.data[0] != '\n') break;
    lexer->content.count -= 1;
    lexer->content.data += 1;
    lexer->loc.line += 1;
    lexer->loc.col = 0;
  }
}

static void lexer_consume_char(Lexer *lexer, String_View *sv) {
  assert(lexer->content.count > 0);
  if (lexer->content.data[0] == '\n') {
    lexer->loc.col = 0;
    lexer->loc.line += 1;
  } else {
    lexer->loc.col += 1;
  }
  lexer->content.count -= 1;
  lexer->content.data += 1;
  sv->count += 1;
}

static void lexer_consume_line(Lexer *lexer, String_View *sv) {
  while (true) {
    const String_View c = sv_chop_by_delim(&lexer->content, '\n');
    lexer->loc.col = 0;
    lexer->loc.line += 1;
    sv->count += c.count;
    if (c.count == 0 || c.data[c.count-1] != '\\') break;
    sv->count += 1; // newline was escaped, consume it and continue
  }
}

static void lexer_consume_number_lit(Lexer *lexer, String_View *sv) {
  if (lexer->content.count >= 2 && lexer->content.data[0] == '0') {
    const char t = toupper(lexer->content.data[1]);
    if (t == 'X') {
      lexer_consume_char(lexer, sv); // 0
      lexer_consume_char(lexer, sv); // x
      while (lexer->content.count && is_hex_number(lexer->content.data[0])) lexer_consume_char(lexer, sv);
    } else if (t == 'B') {
      lexer_consume_char(lexer, sv); // 0
      lexer_consume_char(lexer, sv); // b
      while (lexer->content.count && is_bin_number(lexer->content.data[0])) lexer_consume_char(lexer, sv);
    } else while (lexer->content.count && is_oct_number(lexer->content.data[0])) lexer_consume_char(lexer, sv);
    while (lexer->content.count && is_number_suffix(lexer->content.data[0])) lexer_consume_char(lexer, sv);
    return;
  }
  while (lexer->content.count && is_number(lexer->content.data[0])) lexer_consume_char(lexer, sv);
  while (lexer->content.count && is_number_suffix(lexer->content.data[0])) lexer_consume_char(lexer, sv);
}

static void lexer_consume_directive(Lexer *lexer, String_View *sv) {
  assert(lexer->content.count > 0 && lexer->content.data[0] == '#');
  lexer_consume_char(lexer, sv); // #
  lexer_consume_line(lexer, sv);
}

static void lexer_consume_line_comment(Lexer *lexer, String_View *sv) {
  assert(lexer->content.count > 0 && lexer->content.data[0] == '/');
  lexer_consume_char(lexer, sv); // /
  lexer_consume_line(lexer, sv);
}

static void lexer_consume_block_comment(Lexer *lexer, String_View *sv) {
  assert(lexer->content.count > 0 && lexer->content.data[0] == '*');
  lexer_consume_char(lexer, sv); // *
  while (lexer->content.count) {
    const char c = lexer->content.data[0];
    lexer_consume_char(lexer, sv);
    if (c == '*') SV_PEEK(*sv, 0, cc, if (cc == '/') {
      lexer_consume_char(lexer, sv);
      return;
    });
  }
}

static void lexer_consume_string(Lexer *lexer, String_View *sv) {
  assert(lexer->content.count > 0 && lexer->content.data[0] == '"');
  lexer_consume_char(lexer, sv); // "
  while (lexer->content.count) {
    const char c = lexer->content.data[0];
    lexer_consume_char(lexer, sv);
    if (c == '"') return;
    if (c == '\\') {
      if (!lexer->content.count) lexer_print_err(lexer->loc, stderr, "Unclosed string literal");
      lexer_consume_char(lexer, sv);
    }
  }
  lexer_print_err(lexer->loc, stderr, "Unclosed string literal");
}

TokenOrEnd lexer_peek_token(Lexer *lexer) {
  if (lexer->peek.has_value) return lexer->peek;
  lexer->peek.has_value = false;

  lexer_remove_space(lexer);
  if (lexer->content.count == 0) return lexer->peek; // no value

  TokenKind last_kind = TK_NAME;
  String_View token = (String_View) {
    .count = 0,
    .data = lexer->content.data,
  };
  const Location token_loc = lexer->loc;
  const char c = lexer->content.data[0];
  if (isspace(c)) {
    assert(0 && "unreachable: lexer_remove_space should have removed this");
  } else if (isalpha(c) || c == '_') {
    while (lexer->content.count && is_ident(lexer->content.data[0])) lexer_consume_char(lexer, &token); // TODO: \+newline is permissible
  } else if (isdigit(c)) {
    last_kind = TK_LIT;
    lexer_consume_number_lit(lexer, &token);
  } else if (c == '(' || c == ')' || c == '[' || c == ']' || c == '{' || c == '}') {
    PRODUCE(TK_PAREN);
  } else if (c == ';' || c == ',' || c == ':' || c == '?') {
    PRODUCE(TK_SEP);
  } else if (c == '.') {
    if (lexer->content.count >= 3 && lexer->content.data[1] == '.' && lexer->content.data[2] == '.') {
      lexer_consume_char(lexer, &token); // .
      lexer_consume_char(lexer, &token); // .
      lexer_consume_char(lexer, &token); // .
    } else PRODUCE(TK_ACCESS);
  } else if (c == '=' || c == '+' || c == '-' || c == '*' || c == '/' || c == '!' || c == '^' || c == '>' || c == '<') {
    last_kind = TK_OP;
    lexer_consume_char(lexer, &token); // op
    SV_PEEK(lexer->content, 0, cc, if (cc == '=') lexer_consume_char(lexer, &token));
    if (c == '-') SV_PEEK(lexer->content, 0, cc, if (cc == '>') {
      last_kind = TK_ACCESS;
      lexer_consume_char(lexer, &token);
    });
    if (c == '/') SV_PEEK(lexer->content, 0, cc,
      if (cc == '/') {
        last_kind = TK_COMMENT;
        lexer_consume_line_comment(lexer, &token);
      } else if (cc == '*') {
        last_kind = TK_COMMENT;
        lexer_consume_block_comment(lexer, &token);
      }
    );
  } else if (c == '&' || c == '|') {
    last_kind = TK_OP;
    lexer_consume_char(lexer, &token); // op
    SV_PEEK(lexer->content, 0, cc, if (cc == c || c == '=') lexer_consume_char(lexer, &token));
  } else if (c == '\'') {
    last_kind = TK_LIT;
    lexer_consume_char(lexer, &token); // '
    SV_PEEK(lexer->content, 0, cc, if (cc == '\\') lexer_consume_char(lexer, &token));
    if (lexer->content.count <= 1 || lexer->content.data[1] != '\'') lexer_print_err(lexer->loc, stderr, "Unclosed character literal");
    lexer_consume_char(lexer, &token); // char
    lexer_consume_char(lexer, &token); // '
  } else if (c == '"') {
    last_kind = TK_LIT;
    lexer_consume_string(lexer, &token);
  } else if (c == '#') {
    last_kind = TK_DIRECTIVE;
    lexer_consume_directive(lexer, &token);
  } else {
    lexer_print_err(lexer->loc, stderr, "Unknown token starts with '%c'", c);
  }

  lexer->peek.has_value = true;

  if (last_kind == TK_NAME) {
    if (sv_eq(token, SV("typedef"))) {
      last_kind = TK_TYPEDF;
    } else if (sv_eq(token, SV("struct"))) {
      last_kind = TK_STRUCT;
    } else if (sv_eq(token, SV("enum"))) {
      last_kind = TK_ENUM;
    } else if (sv_eq(token, SV("true")) || sv_eq(token, SV("false"))) {
      last_kind = TK_LIT;
    }
  }
  lexer->peek.token = (Token) {
    .loc = token_loc,
    .content = token,
    .kind = last_kind,
  };
  return lexer->peek;
}

TokenOrEnd lexer_get_token(Lexer *lexer) {
  TokenOrEnd token = lexer_peek_token(lexer);
  lexer->peek.has_value = false;
  return token;
}

void lexer_print_loc(Location loc, FILE *stream) {
  fprintf(stream, SV_Fmt ":%zu:%zu", SV_Arg(loc.filename), loc.line + 1, loc.col + 1);
}

void lexer_print_err(Location loc, FILE *stream, char *fmt, ...) {
  fprintf(stream, "ERROR: ");
  lexer_print_loc(loc, stream);
  fprintf(stream, ": ");
  va_list args;
  va_start(args, fmt);
  vfprintf(stream, fmt, args);
  va_end(args);
  fprintf(stream, "\n");
  exit(1);
}

void lexer_print_token(Token token, FILE *stream) {
  lexer_print_loc(token.loc, stream);
  fprintf(stream, ": ");
  switch (token.kind)
  {
  case TK_NAME: fprintf(stream, "TK_NAME"); break;
  case TK_DIRECTIVE: fprintf(stream, "TK_DIRECTIVE"); break;
  case TK_COMMENT: fprintf(stream, "TK_COMMENT"); break;
  case TK_TYPEDF: fprintf(stream, "TK_TYPEDF"); break;
  case TK_STRUCT: fprintf(stream, "TK_STRUCT"); break;
  case TK_ENUM: fprintf(stream, "TK_ENUM"); break;
  case TK_PAREN: fprintf(stream, "TK_PAREN"); break;
  case TK_SEP: fprintf(stream, "TK_SEP"); break;
  case TK_OP: fprintf(stream, "TK_OP"); break;
  case TK_ACCESS: fprintf(stream, "TK_ACCESS"); break;
  case TK_LIT: fprintf(stream, "TK_LIT"); break;
  default: assert(0);
  }
  fprintf(stream, " " SV_Fmt "\n", SV_Arg(token.content));
}