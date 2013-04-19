/* Copyright (c) 2012 The Regents of the University of California
 * Kevin Klues <klueska@cs.berkeley.edu>
 */

#ifndef LIBGOMP_LITHE_H
#define LIBGOMP_LITHE_H

#include <sys/queue.h>
#include <parlib/mcs.h>
#include <lithe/lithe.h>
#include <string.h>
#include <stdbool.h>

typedef struct libgomp_lithe_context {
  lithe_context_t context;
  void (*start_routine)(void *);
  void *arg;
  bool completed;
  bool make_zombie;
} libgomp_lithe_context_t;

typedef struct libgomp_lithe_child_sched {
  TAILQ_ENTRY(libgomp_lithe_child_sched) link;
  lithe_sched_t *sched;
  int requested_harts;
} libgomp_lithe_child_sched_t;
TAILQ_HEAD(libgomp_lithe_child_sched_queue, libgomp_lithe_child_sched);
typedef struct libgomp_lithe_child_sched_queue libgomp_lithe_child_sched_queue_t;

struct libgomp_lithe_sched {
  lithe_sched_t sched;
  int num_contexts;
  mcs_lock_t qlock;
  lithe_context_queue_t context_queue;
  libgomp_lithe_child_sched_queue_t child_sched_queue;
};
typedef struct libgomp_lithe_sched libgomp_lithe_sched_t;

void libgomp_lithe_setstacksize(size_t stack_size);
void libgomp_lithe_sched_ctor(libgomp_lithe_sched_t* sched);
void libgomp_lithe_sched_dtor(libgomp_lithe_sched_t* sched);
void libgomp_lithe_context_create(libgomp_lithe_context_t **__context,
  void (*start_routine)(void*), void *arg);
void libgomp_lithe_context_exit();
void libgomp_lithe_context_rebind_sched(libgomp_lithe_context_t *c,
                                        libgomp_lithe_sched_t *s);
void libgomp_lithe_context_signal_started();
void libgomp_lithe_context_signal_completed();
void libgomp_lithe_sched_join_completed();

#endif // LIBGOMP_LITHE_H
