/* Copyright (c) 2012 The Regents of the University of California
 * Kevin Klues <klueska@cs.berkeley.edu>
 */

#ifndef LIBGOMP_LITHE_H
#define LIBGOMP_LITHE_H

#include <sys/queue.h>
#include <parlib/mcs.h>
#include <lithe/mutex.h>
#include <lithe/condvar.h>
#include <string.h>

struct libgomp_lithe_context {
  lithe_context_t context;
  STAILQ_ENTRY(libgomp_lithe_context) link;
  void (*start_routine)(void *);
  void *arg;
  bool make_zombie;
};
STAILQ_HEAD(libgomp_lithe_context_list, libgomp_lithe_context);
typedef struct libgomp_lithe_context libgomp_lithe_context_t;
typedef struct libgomp_lithe_context_list libgomp_lithe_context_list_t;

struct libgomp_lithe_child_sched {
  STAILQ_ENTRY(libgomp_lithe_child_sched) link;
  lithe_sched_t *sched;
  int requested_harts;
};
STAILQ_HEAD(libgomp_lithe_child_sched_list, libgomp_lithe_child_sched);
typedef struct libgomp_lithe_child_sched libgomp_lithe_child_sched_t;
typedef struct libgomp_lithe_child_sched_list libgomp_lithe_child_sched_list_t;

struct libgomp_lithe_sched {
  lithe_sched_t sched;
  int num_contexts;
  lithe_mutex_t mutex;
  lithe_condvar_t condvar;
  mcs_lock_t qlock;
  libgomp_lithe_context_list_t context_list;
  libgomp_lithe_child_sched_list_t child_sched_list;
};
typedef struct libgomp_lithe_sched libgomp_lithe_sched_t;

void libgomp_lithe_setstacksize(size_t stack_size);
void libgomp_lithe_sched_ctor(libgomp_lithe_sched_t* sched);
void libgomp_lithe_sched_dtor(libgomp_lithe_sched_t* sched);
void libgomp_lithe_context_create(libgomp_lithe_context_t **__context,
  void (*start_routine)(void*), void *arg);
void libgomp_lithe_context_exit();
void libgomp_lithe_context_rebind_sched();
void libgomp_lithe_context_signal_started();
void libgomp_lithe_context_signal_completed();
void libgomp_lithe_sched_join_completed();

#endif // LIBGOMP_LITHE_H
