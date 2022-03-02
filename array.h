#pragma once
#include <inttypes.h>
#include <stddef.h>
#include <stdlib.h>
#include <errno.h>
#include <stdio.h>

#ifndef ARRAY_INIT_CAP
#define ARRAY_INIT_CAP 100
#endif // ARRAY_INIT_CAP

#define MAKE_ARRAY(T)   \
  T *items;             \
  size_t items_count;   \
  size_t items_cap;

#define ARRAY_PUSH(arr, item) do                                                                             \
  {                                                                                                          \
    size_t __i = _array_extend((void **)&(arr).items, &(arr).items_count, &(arr).items_cap, sizeof(item));   \
    (arr).items[__i] = item;                                                                                 \
  } while (0);

size_t _array_extend(void **arr, size_t *cnt, size_t *cap, size_t size) {
  if (*cnt >= *cap) {
    size_t ncap = *cap < ARRAY_INIT_CAP ? ARRAY_INIT_CAP : *cap * 2;
    void **ptr = (void **)realloc(*arr, ncap * size);
    if (ptr == NULL) {
      perror("realloc _array_extend");
      exit(1);
    }
    *arr = ptr;
    *cap = ncap;
  }
  return (*cnt)++;
}

