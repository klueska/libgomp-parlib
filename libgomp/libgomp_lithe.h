/* Copyright (c) 2012 The Regents of the University of California
 * Kevin Klues <klueska@cs.berkeley.edu>
 */

#ifndef LIBGOMP_LITHE_H
#define LIBGOMP_LITHE_H

#include "internal/assert.h"
#include <sys/queue.h>
#include <parlib/mcs.h>
#include <lithe/lithe.h>
#include <lithe/fork_join_sched.h>
#include <string.h>
#include <stdbool.h>

typedef struct libgomp_lithe_context {
  lithe_fork_join_context_t context;
  bool completed;
} libgomp_lithe_context_t;

typedef struct libgomp_lithe_sched {
  lithe_fork_join_sched_t sched;
  int refcnt;
} libgomp_lithe_sched_t;

void libgomp_lithe_setstacksize(size_t stack_size);
libgomp_lithe_sched_t *libgomp_lithe_sched_alloc();
void libgomp_lithe_sched_incref(libgomp_lithe_sched_t* sched);
void libgomp_lithe_sched_decref(libgomp_lithe_sched_t* sched);
libgomp_lithe_context_t*
  libgomp_lithe_context_create(libgomp_lithe_sched_t *sched,
                               void (*start_routine)(void*),
                               void *arg);
void libgomp_lithe_context_rebind_sched(libgomp_lithe_context_t *c,
                                        libgomp_lithe_sched_t *s);
void libgomp_lithe_context_signal_started();
void libgomp_lithe_context_signal_completed();
void libgomp_lithe_context_signal_completed_immediate();
void libgomp_lithe_sched_join_completed();

#endif // LIBGOMP_LITHE_H
