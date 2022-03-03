#include <stdio.h>
#include "test.h"

int main() {
  struct t1 x = {0};
  struct t2 y = {0};
  asdf z = {0};
  x.w = 1;
  y.w2 = 5;
  printf("%d\n", CEST_AS_struct_t1S(&x)->w);
  printf("%d\n", CEST_AS_struct_t1S(&y)->w);
  printf("%d\n", CEST_AS_struct_t1S(&z)->w);
  // printf("%d\n", CEST_AS_struct_t1S(1));
  printf("%d\n", CEST_AS_struct_t1(x).w);
  printf("%d\n", CEST_AS_struct_t1(y).w);
  printf("%d\n", CEST_AS_struct_t1(z).w);
}
