/*
 * Copyright (c) 2025, Alliance for Open Media. All rights reserved
 *
 * This source code is subject to the terms of the BSD 3-Clause Clear License
 * and the Alliance for Open Media Patent License 1.0. If the BSD 3-Clause Clear
 * License was not distributed with this source code in the LICENSE file, you
 * can obtain it at www.aomedia.org/license/software-license/bsd-3-c-c. If the
 * Alliance for Open Media Patent License 1.0 was not distributed with this
 * source code in the PATENTS file, you can obtain it at
 * www.aomedia.org/license/patent.
 */

#ifndef __DEFINITIONS_H__
#define __DEFINITIONS_H__

#include <stdint.h>
#include <stdlib.h>

/* malloc() and realloc() of n elements of the given size, or NULL when the
 * byte count overflows size_t, as calloc() does. */
static inline void *def_malloc_array(size_t n, size_t size) {
  return n > SIZE_MAX / size ? NULL : malloc(n * size);
}

static inline void *def_realloc_array(void *ptr, size_t n, size_t size) {
  return n > SIZE_MAX / size ? NULL : realloc(ptr, n * size);
}

#define def_mallocz(type, n) ((type *)calloc(n, sizeof(type)))
#define def_malloc(type, n) ((type *)def_malloc_array((n), sizeof(type)))
#define def_realloc(ptr, type, n) \
  ((type *)def_realloc_array((ptr), (n), sizeof(type)))
#define def_free(ptr)   \
  do {                  \
    if (ptr) free(ptr); \
  } while (0)

#define def_max(a, b) (((a) > (b)) ? (a) : (b))
#define def_min(a, b) (((a) < (b)) ? (a) : (b))

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#endif  //__DEFINITIONS_H__