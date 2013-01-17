/* Copyright (c) 2012 The Regents of the University of California
 * Kevin Klues <klueska@cs.berkeley.edu>
 */

#ifndef LIBGOMP_LITHE_H
#define LIBGOMP_LITHE_H

#include <string.h>

struct libgomp_lithe_sched;
typedef struct libgomp_lithe_sched libgomp_lithe_sched_t;

struct libgomp_lithe_context;
typedef struct libgomp_lithe_context libgomp_lithe_context_t;

void libgomp_lithe_setstacksize(size_t stack_size);
void libgomp_lithe_sched_ctor(libgomp_lithe_sched_t* sched);
void libgomp_lithe_sched_dtor(libgomp_lithe_sched_t* sched);
void libgomp_lithe_context_create(libgomp_lithe_context_t **__context,
  void (*start_routine)(void*), void *arg);
void libgomp_lithe_sched_joinAll();

#endif // LIBGOMP_LITHE_H
