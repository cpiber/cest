#pragma once
#include <inttypes.h>
#include <stddef.h>
#include <stdlib.h>
#include <errno.h>
#include <stdio.h>
#include <assert.h>

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
#define ARRAY_EXTEND(arr, n) _array_extend_n((void **)&(arr).items, &(arr).items_count, &(arr).items_cap, sizeof((arr).items[0]), n)

void _array_extend_n(void **arr, size_t *cnt, size_t *cap, size_t size, size_t n) {
  assert(*cnt <= *cap);
  if (*cnt + n > *cap) {
    size_t ncap = *cap < ARRAY_INIT_CAP ? ARRAY_INIT_CAP : *cap * 2;
    if (ncap < n) ncap = n * 2;
    void **ptr = (void **)realloc(*arr, ncap * size);
    if (ptr == NULL) {
      perror("realloc _array_extend");
      exit(1);
    }
    *arr = ptr;
    *cap = ncap;
  }
}

size_t _array_extend(void **arr, size_t *cnt, size_t *cap, size_t size) {
  _array_extend_n(arr, cnt, cap, size, 1);
  return (*cnt)++;
}

