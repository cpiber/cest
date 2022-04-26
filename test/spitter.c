#include <stdio.h>
#include "../lexer.h"
#define NO_MAIN
#include "../cest.c"

int main(int argc, char *argv[]) {
  if (argc < 1) {
    fprintf(stderr, "too few arguments provided!\n");
    exit(1);
  }
  String_View file = load_file(argv[1]);
  Lexer lexer = (Lexer) { .content = file, .loc = { .filename = sv_from_cstr(argv[1]) } };
  TokenOrEnd token = lexer_get_token(&lexer);
  while (token.has_value) {
    lexer_dump_token(token.token, stdout);
    token = lexer_get_token(&lexer);
  }
  return 0;
}
