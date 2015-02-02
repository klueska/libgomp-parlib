/* Copyright (c) 2012 The Regents of the University of California
 * Kevin Klues <klueska@cs.berkeley.edu>
 */

#include <sys/mman.h>
#include "libgomp_lithe.h"
#include <parlib/dtls.h>
#include <lithe/lithe.h>
#include <lithe/defaults.h>

static size_t __context_stack_size = 1<<20;
struct wfl sched_zombie_list = WFL_INITIALIZER(sched_zombie_list);
struct wfl context_zombie_list = WFL_INITIALIZER(context_zombie_list);

static void context_block(lithe_sched_t *__this, lithe_context_t *context);
static void context_exit(lithe_sched_t *__this, lithe_context_t *context);

static const lithe_sched_funcs_t libgomp_lithe_sched_funcs = {
  .hart_request        = lithe_fork_join_sched_hart_request,
  .hart_enter          = lithe_fork_join_sched_hart_enter,
  .hart_return         = lithe_fork_join_sched_hart_return,
  .sched_enter         = lithe_fork_join_sched_sched_enter,
  .sched_exit          = lithe_fork_join_sched_sched_exit,
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

static libgomp_lithe_context_t *__ctx_alloc(size_t stacksize)
{
	libgomp_lithe_context_t *ctx = wfl_remove(&context_zombie_list);
	if (!ctx) {
		int offset = rand_r(&rseed(0)) % max_vcores() * ARCH_CL_SIZE;
		stacksize += sizeof(libgomp_lithe_context_t) + offset;
		stacksize = ROUNDUP(stacksize, PGSIZE);
		void *stackbot = mmap(
			0, stacksize, PROT_READ|PROT_WRITE|PROT_EXEC,
			MAP_PRIVATE|MAP_ANONYMOUS, -1, 0
		);
		if (stackbot == MAP_FAILED)
			abort();
		ctx = stackbot + stacksize - sizeof(libgomp_lithe_context_t) - offset;
		ctx->context.context.stack.bottom = stackbot;
		ctx->context.context.stack.size = stacksize - sizeof(*ctx) - offset;
	}
    return ctx;
}

static void __ctx_free(libgomp_lithe_context_t *ctx)
{
	if (wfl_size(&context_zombie_list) < 1000) {
		wfl_insert(&context_zombie_list, ctx);
	} else {
		assert(!munmap(ctx->context.context.stack.bottom,
					   ctx->context.context.stack.size));
	}
}

libgomp_lithe_context_t*
  libgomp_lithe_context_create(libgomp_lithe_sched_t *sched,
                               void (*start_routine)(void*),
                               void *arg)
{
  libgomp_lithe_context_t *c = __ctx_alloc(__context_stack_size);
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
  ctx->state = FJS_CTX_CREATED;
  c->context.preferred_vcq = -1;
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
  __ctx_free((libgomp_lithe_context_t*)context);
  libgomp_lithe_sched_decref((libgomp_lithe_sched_t*)__this);
}

libgomp_lithe_sched_t *libgomp_lithe_sched_alloc()
{
  /* Allocate all the scheduler data together. */
  struct sched_data {
    libgomp_lithe_sched_t sched;
    libgomp_lithe_context_t main_context;
    struct lithe_fork_join_vc_mgmt vc_mgmt[];
  };

  /* Use a zombie list to reuse old schedulers if available, otherwise, create
   * a new one. */
  struct sched_data *s = wfl_remove(&sched_zombie_list);
  if (!s) {
    s = parlib_aligned_alloc(PGSIZE,
            sizeof(*s) + sizeof(struct lithe_fork_join_vc_mgmt) * max_vcores());
    s->sched.sched.vc_mgmt = &s->vc_mgmt[0];
    s->sched.sched.sched.funcs = &libgomp_lithe_sched_funcs;
  }

  /* Initialize some libgomp specific fields before initializing the generic
   * underlying fjs. */
  s->sched.refcnt = 1;
  s->main_context.completed = false;
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
    if (wfl_size(&sched_zombie_list) < 100)
      wfl_insert(&sched_zombie_list, sched);
    else
      free(sched);
  }
}

