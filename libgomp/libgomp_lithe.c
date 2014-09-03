/* Copyright (c) 2012 The Regents of the University of California
 * Kevin Klues <klueska@cs.berkeley.edu>
 */

#include "libgomp_lithe.h"
#include <parlib/dtls.h>
#include <lithe/lithe.h>
#include <lithe/defaults.h>

static size_t __context_stack_size = 1<<20;

static void context_block(lithe_sched_t *__this, lithe_context_t *context);
static void context_exit(lithe_sched_t *__this, lithe_context_t *context);

static const lithe_sched_funcs_t libgomp_lithe_sched_funcs = {
  .hart_request        = lithe_fork_join_sched_hart_request,
  .hart_enter          = lithe_fork_join_sched_hart_enter,
  .hart_return         = lithe_fork_join_sched_hart_return,
  .child_enter         = lithe_fork_join_sched_child_enter,
  .child_exit          = lithe_fork_join_sched_child_exit,
  .context_block       = context_block,
  .context_unblock     = lithe_fork_join_sched_context_unblock,
  .context_yield       = lithe_fork_join_sched_context_yield,
  .context_exit        = context_exit
};

void libgomp_lithe_setstacksize(size_t stack_size)
{
  __context_stack_size = stack_size;
}

libgomp_lithe_context_t*
  libgomp_lithe_context_create(libgomp_lithe_sched_t *sched,
                               void (*start_routine)(void*),
                               void *arg)
{
  /* Create a new lithe context and initialize it from scratch */
  size_t stack_size = __context_stack_size;
  void *storage = malloc(sizeof(libgomp_lithe_context_t) + stack_size);
  if (storage == NULL)
    abort();

  libgomp_lithe_context_t *c = storage;
  c->context.context.stack.bottom = storage + sizeof(libgomp_lithe_context_t);
  c->context.context.stack.size = stack_size;
  c->completed = false;
  libgomp_lithe_sched_incref(sched);

  lithe_fork_join_context_init(&sched->sched, &c->context, start_routine, arg);

  return c;
}

void libgomp_lithe_context_rebind_sched(libgomp_lithe_context_t *c,
                                        libgomp_lithe_sched_t *s)
{
  libgomp_lithe_sched_decref((libgomp_lithe_sched_t*)c->context.context.sched);
  libgomp_lithe_sched_incref(s);
  lithe_context_reassociate(&c->context.context, &s->sched.sched);
  c->completed = false;
  __sync_fetch_and_add(&s->sched.num_contexts, 1);
}

void libgomp_lithe_context_signal_completed()
{
  libgomp_lithe_context_t *c = (libgomp_lithe_context_t*)lithe_context_self();
  c->completed = true;
}

void libgomp_lithe_context_signal_completed_immediate()
{
  libgomp_lithe_sched_t *sched = (libgomp_lithe_sched_t*)lithe_sched_current();
  libgomp_lithe_context_t *c = (libgomp_lithe_context_t*)lithe_context_self();
  c->completed = true;
  lithe_fork_join_sched_join_one(&sched->sched);
}

void libgomp_lithe_sched_join_completed()
{
  libgomp_lithe_context_t *c = (libgomp_lithe_context_t*)lithe_context_self();
  c->completed = true;
  lithe_context_block(NULL, NULL);
}

static void context_block(lithe_sched_t *__this, lithe_context_t *__context)
{
  libgomp_lithe_sched_t *sched = (libgomp_lithe_sched_t*)__this;
  libgomp_lithe_context_t *context = (libgomp_lithe_context_t*)__context;

  if(context->completed)
    lithe_fork_join_sched_join_one(&sched->sched);
}

static void context_exit(lithe_sched_t *__this, lithe_context_t *context)
{
  lithe_fork_join_context_cleanup((lithe_fork_join_context_t*)context);
  free(context);
  libgomp_lithe_sched_decref((libgomp_lithe_sched_t*)__this);
}

libgomp_lithe_sched_t *libgomp_lithe_sched_alloc()
{
  struct {
    libgomp_lithe_sched_t sched;
    libgomp_lithe_context_t main_context;
  } *s = malloc(sizeof(*s));
  if (s == NULL)
    abort();

  s->sched.refcnt = 1;
  s->sched.sched.sched.funcs = &libgomp_lithe_sched_funcs;
  s->main_context.completed = false;
  memset(&s->main_context.context, 0, sizeof(libgomp_lithe_context_t));
  lithe_fork_join_sched_init(&s->sched.sched, &s->main_context.context);

  return &s->sched;
}

void libgomp_lithe_sched_incref(libgomp_lithe_sched_t *sched)
{
  int old = __sync_fetch_and_add(&sched->refcnt, 1);
  assert(old);
}

void libgomp_lithe_sched_decref(libgomp_lithe_sched_t *sched)
{
  if (__sync_add_and_fetch(&sched->refcnt, -1) == 0) {
    lithe_fork_join_sched_cleanup(&sched->sched);
    free(sched);
  }
}

