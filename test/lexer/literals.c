#include "lexertest.h"

int main() {
  Lexer lexer;

  // Number literals
  lexer = lexer_create(TEST, SV("123"));
  EXPECT_TOKEN(TK_LIT, "123"); EXPECT_EMPTY;
  lexer = lexer_create(TEST, SV("123\n"));
  EXPECT_TOKEN(TK_LIT, "123"); EXPECT_EMPTY;
  lexer = lexer_create(TEST, SV("123L"));
  EXPECT_TOKEN(TK_LIT, "123L"); EXPECT_EMPTY;
  lexer = lexer_create(TEST, SV("123LU"));
  EXPECT_TOKEN(TK_LIT, "123LU"); EXPECT_EMPTY;
  lexer = lexer_create(TEST, SV("123.0f"));
  EXPECT_TOKEN(TK_LIT, "123.0f"); EXPECT_EMPTY;
  lexer = lexer_create(TEST, SV("0x123"));
  EXPECT_TOKEN(TK_LIT, "0x123"); EXPECT_EMPTY;
  lexer = lexer_create(TEST, SV("0x123ABC"));
  EXPECT_TOKEN(TK_LIT, "0x123ABC"); EXPECT_EMPTY;
  lexer = lexer_create(TEST, SV("0b101"));
  EXPECT_TOKEN(TK_LIT, "0b101"); EXPECT_EMPTY;
  lexer = lexer_create(TEST, SV("0b12"));
  EXPECT_TOKEN(TK_LIT, "0b1"); EXPECT_TOKEN(TK_LIT, "2"); EXPECT_EMPTY;
  lexer = lexer_create(TEST, SV("123ABC"));
  EXPECT_TOKEN(TK_LIT, "123"); EXPECT_TOKEN(TK_NAME, "ABC"); EXPECT_EMPTY;
  lexer = lexer_create(TEST, SV("0x123ABCXYZ"));
  EXPECT_TOKEN(TK_LIT, "0x123ABC"); EXPECT_TOKEN(TK_NAME, "XYZ"); EXPECT_EMPTY;

  // character literals
  lexer = lexer_create(TEST, SV("'a'"));
  EXPECT_TOKEN(TK_LIT, "'a'"); EXPECT_EMPTY;
  lexer = lexer_create(TEST, SV("'\\a'"));
  EXPECT_TOKEN(TK_LIT, "'\\a'"); EXPECT_EMPTY;
  lexer = lexer_create(TEST, SV("'\\''"));
  EXPECT_TOKEN(TK_LIT, "'\\''"); EXPECT_EMPTY;
  lexer = lexer_create(TEST, SV("'ab'"));
  EXPECT_ERROR;
  lexer = lexer_create(TEST, SV("'a"));
  EXPECT_ERROR;

  // string literals
  lexer = lexer_create(TEST, SV("\"a\""));
  EXPECT_TOKEN(TK_LIT, "\"a\""); EXPECT_EMPTY;
  lexer = lexer_create(TEST, SV("\"a\\\n\""));
  EXPECT_TOKEN(TK_LIT, "\"a\\\n\""); EXPECT_EMPTY;
  lexer = lexer_create(TEST, SV("\"a\\\"\""));
  EXPECT_TOKEN(TK_LIT, "\"a\\\"\""); EXPECT_EMPTY;
  lexer = lexer_create(TEST, SV("\"a\\\"test\""));
  EXPECT_TOKEN(TK_LIT, "\"a\\\"test\""); EXPECT_EMPTY;
  lexer = lexer_create(TEST, SV("\"a\\\"\\\\\""));
  EXPECT_TOKEN(TK_LIT, "\"a\\\"\\\\\""); EXPECT_EMPTY;
  lexer = lexer_create(TEST, SV("\"a"));
  EXPECT_ERROR;
}
