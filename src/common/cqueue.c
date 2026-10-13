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

/**
 * @file cqueue.c
 * @brief Queue APIs.
 * @version 2.0.0
 * @date Created 23/05/2023
 **/

#include "cqueue.h"

#include <stdlib.h>

#include "cvalue.h"

typedef struct CNode node_t;
struct CNode {
  value_wrap_t value;
  node_t *next;
};

struct CQueue {
  node_t *front;
  node_t *rear;
  int count;
  /* Nodes popped off the queue or reserved with queue_reserve(), reused by
   * the next push. A queue whose length stays within the nodes it has held
   * before does not allocate on push or free on pop. */
  node_t *spare;
};

/* Takes a spare node, or allocates one when there is none. */
static node_t *_node_get(queue_t *q) {
  node_t *n = q->spare;
  if (!n) return (node_t *)calloc(1, sizeof(node_t));
  q->spare = n->next;
  n->next = 0;
  return n;
}

static void _node_put(queue_t *q, node_t *n) {
  n->next = q->spare;
  q->spare = n;
}

queue_t *queue_new() {
  queue_t *q = 0;
  q = (queue_t *)calloc(1, sizeof(queue_t));
  return q;
}

void queue_free(queue_t *q, func_value_wrap_ptr_free_t free_func) {
  value_wrap_t v;
  while (queue_pop(q, &v) == 0)
    if (free_func && v.ptr) free_func(v.ptr);
  if (q) {
    while (q->spare) {
      node_t *n = q->spare;
      q->spare = n->next;
      free(n);
    }
  }
  free(q);
}

int queue_reserve(queue_t *q, int n) {
  int spares = 0;
  if (!q || n < 0) return -22;  // -EINVAL
  for (node_t *s = q->spare; s; s = s->next) ++spares;
  for (; spares < n; ++spares) {
    node_t *node = (node_t *)calloc(1, sizeof(node_t));
    if (!node) return -12;  // -ENOMEM
    _node_put(q, node);
  }
  return 0;
}

int queue_push(queue_t *q, value_wrap_t v) {
  if (!q) return -22;  // -EINVAL
  node_t *n = 0;
  n = _node_get(q);
  if (!n) return -12;  // -ENOMEM
  n->value = v;
  if (!q->rear)
    q->rear = n;
  else {
    q->rear->next = n;
    q->rear = n;
  }
  ++q->count;
  if (!q->front) q->front = q->rear;
  return q->count - 1;
}

int queue_pop(queue_t *q, value_wrap_t *v) {
  node_t *n = 0;

  if (!q || queue_is_empty(q)) return -22;  // -EINVAL

  n = q->front;
  *v = n->value;
  q->front = n->next;
  if (!q->front) q->rear = 0;
  --q->count;
  _node_put(q, n);
  return 0;
}

value_wrap_t *queue_at(queue_t *q, int i) {
  node_t *n = 0;

  if (!q || queue_length(q) <= i) return 0;

  n = q->front;
  for (int k = 0; k < i; ++k) {
    n = n->next;
  }
  return &n->value;
}

int queue_is_empty(queue_t *q) {
  if (!q) return 1;
  return !q->count;
}

int queue_length(queue_t *q) {
  if (!q) return 0;
  return q->count;
}

int queue_custom_push(queue_t *q, value_wrap_t v, queue_custom_func_t cmp) {
  int idx = 0;
  node_t *n = 0;
  node_t *p = 0;

  if (!q || !cmp) return -22;  // -EINVAL

  n = q->front;
  while (n) {
    if (cmp(v, n->value) < 0) {
      p = n;
      n = n->next;
      ++idx;
    } else {
      n = _node_get(q);
      if (!n) return -12;  // -ENOMEM
      n->value = v;
      if (p) {
        n->next = p->next;
        p->next = n;
      } else {
        n->next = q->front;
        q->front = n;
      }
      break;
    }
  }

  if (n) ++q->count;

  return n ? idx : queue_push(q, v);
}

value_wrap_t *queue_custom_find(queue_t *q, value_wrap_t v,
                                queue_custom_func_t cmp) {
  node_t *n = 0;
  value_wrap_t *rv = 0;

  if (!q || !cmp) return 0;

  n = q->front;
  while (n) {
    if (cmp(v, n->value)) {
      rv = &n->value;
      break;
    } else {
      n = n->next;
    }
  }
  return rv;
}
