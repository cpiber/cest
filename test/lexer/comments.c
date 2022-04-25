#include "lexertest.h"

int main() {
  Lexer lexer;

  // line comments
  lexer = lexer_create(TEST, SV("// asf test"));
  EXPECT_TOKEN(TK_COMMENT, "// asf test"); EXPECT_EMPTY;
  lexer = lexer_create(TEST, SV("// asf test\n"));
  EXPECT_TOKEN(TK_COMMENT, "// asf test"); EXPECT_EMPTY;
  lexer = lexer_create(TEST, SV("//"));
  EXPECT_TOKEN(TK_COMMENT, "//"); EXPECT_EMPTY;
  lexer = lexer_create(TEST, SV("// asf test\\\n23"));
  EXPECT_TOKEN(TK_COMMENT, "// asf test\\\n23"); EXPECT_EMPTY;

  // block comments
  lexer = lexer_create(TEST, SV("/* asf test */"));
  EXPECT_TOKEN(TK_COMMENT, "/* asf test */"); EXPECT_EMPTY;
  lexer = lexer_create(TEST, SV("/* asf \n test */"));
  EXPECT_TOKEN(TK_COMMENT, "/* asf \n test */"); EXPECT_EMPTY;
  lexer = lexer_create(TEST, SV("/* asf \n test \\*/"));
  EXPECT_TOKEN(TK_COMMENT, "/* asf \n test \\*/"); EXPECT_EMPTY;
  lexer = lexer_create(TEST, SV("/* asf"));
  EXPECT_TOKEN(TK_COMMENT, "/* asf"); EXPECT_EMPTY;
  lexer = lexer_create(TEST, SV("/* asf * */"));
  EXPECT_TOKEN(TK_COMMENT, "/* asf * */"); EXPECT_EMPTY;
}
