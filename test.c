#include <stdio.h>

struct t1 {
  int w;
};
typedef struct t2 {
  int w;
  int w2;
} asdf;
typedef struct {
  int w;
} t3;

#define CAST_AS_T1S(X) _Generic((X), struct t1*: (X), struct t2*: (struct t1*) (X))
#define CAST_AS_T1(X) _Generic((X), struct t1: (X), struct t2: *(struct t1*) &(X))

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
