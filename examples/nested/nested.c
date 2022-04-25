#include "nested.h"
#include <stdio.h>

int main() {
  Child2 chld = { .b1 = 0, .b2 = 69, .b3 = 420 };
  printf("%zu\n", CEST_AS_Child1(chld).b2);
}
