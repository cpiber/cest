#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdio.h>
#include "../test.h"
#include "../../lexer.h"

#define SV_IMPLEMENTATION
#include "../../sv.h"

#define EXPECT_TOKEN(knd, cnd)  {                                                         \
    TokenOrEnd token = lexer_get_token(&lexer);                                           \
    assert(token.has_value && "Expected token to have value");                            \
    assert(token.token.kind == knd && "Expected token to have kind " #knd);               \
    assert(sv_eq(token.token.content, SV(cnd)) && "Expected token to have content " cnd); \
  } while (0)
#define EXPECT_EMPTY  {                                    \
    TokenOrEnd token = lexer_get_token(&lexer);            \
    assert(!token.has_value && "Expected no more tokens"); \
  } while (0)
#define EXPECT_ERROR {                                                                       \
    pid_t pid = fork();                                                                      \
    if (pid < 0)                                                                             \
      perror(TESTC ": fork");                                                                \
    if (pid == 0) {                                                                          \
      fclose(stderr); /* TODO: capture message */                                            \
      lexer_get_token(&lexer);                                                               \
      exit(0);                                                                               \
    }                                                                                        \
    int status;                                                                              \
    waitpid(pid, &status, 0);                                                                \
    assert(WIFEXITED(status) == 1 && "Expected process to have exited");                     \
    assert(WEXITSTATUS(status) == 1 && "Expected process to have exited with error code 1"); \
  } while (0)
