#include <stdio.h>
#include "defs.h"

void print(Base *this) {
  printf("%s  ;;  ", this->msg);
  printf("count : %zu\n", ++this->count);
}

void special_print(Child *this) {
  printf("from child: %s  ;;  ", this->second);
  print(CEST_AS_BaseS(this));
}

int main() {
  Base base = {
    .msg = "Base instance ",
  };
  Child chl = {
    .msg = "Child instance",
    .second = "hi",
  };
  print(&base);
  print(CEST_AS_BaseS(&chl));
  special_print(&chl);
  printf("Child counter end : %zu / %zu\n", CEST_AS_Base(chl).count, chl.count);
}
