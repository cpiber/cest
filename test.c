#include <stdio.h>
#include <test.h>

int main() {
  struct t1 x = {0};
  struct t2 y = {0};
  asdf z = {0};
  printf("%d\n", CAST_AS_T1S(&x)->w);
  printf("%d\n", CAST_AS_T1S(&y)->w);
  printf("%d\n", CAST_AS_T1S(&z)->w);
  // printf("%d\n", CAST_AS_T1S(1));
  printf("%d\n", CAST_AS_T1(x).w);
  printf("%d\n", CAST_AS_T1(y).w);
  printf("%d\n", CAST_AS_T1(z).w);
}
