/* Copyright (c) 2012 The Regents of the University of California
 * Kevin Klues <klueska@cs.berkeley.edu>
 */

#include "libgomp_lithe.h"
#include "internal/assert.h"
#include <parlib/dtls.h>
#include <lithe/lithe.h>
#include <lithe/defaults.h>

static size_t __context_stack_size = 1<<20;

mcs_lock_t zombie_context_queue_lock;
static lithe_context_queue_t zombie_context_queue = 
  TAILQ_HEAD_INITIALIZER(zombie_context_queue);

static int hart_request(lithe_sched_t *__this, lithe_sched_t *child, int k);
static void child_enter(lithe_sched_t *__this, lithe_sched_t *child);
static void child_exit(lithe_sched_t *__this, lithe_sched_t *child);
static void hart_return(lithe_sched_t *__this, lithe_sched_t *child);
static void hart_enter(lithe_sched_t *__this);
static void context_block(lithe_sched_t *__this, lithe_context_t *context);
static void context_unblock(lithe_sched_t *__this, lithe_context_t *context);
static void context_yield(lithe_sched_t *__this, lithe_context_t *context);
static void context_exit(lithe_sched_t *__this, lithe_context_t *context);

static const lithe_sched_funcs_t libgomp_lithe_sched_funcs = {
  .hart_request        = hart_request,
  .hart_enter          = hart_enter,
  .hart_return         = hart_return,
  .child_enter         = child_enter,
  .child_exit          = child_exit,
  .context_block       = context_block,
  .context_unblock     = context_unblock,
  .context_yield       = context_yield,
  .context_exit        = context_exit
};

typedef void (*start_routine_t)(void*);

static void start_routine_wrapper(void *__arg)
{
  libgomp_lithe_context_t *self = (libgomp_lithe_context_t*)__arg;

  self->start_routine(self->arg);
  destroy_dtls();
}

static int maybe_request_harts(int k)
{
  int ret = 0;
  libgomp_lithe_sched_t *sched = (libgomp_lithe_sched_t*)lithe_sched_current();
  if(((num_harts() < max_harts()) &&
      (sched->num_contexts >= num_harts())))
    ret = lithe_hart_request(k);
  return ret;
}
  
static libgomp_lithe_context_t *maybe_recycle_context(size_t stack_size)
{
  lithe_context_t *c = NULL;

  /* Try and pull a context from the zombie queue and recycle for it use in the
   * current scheduler */
  mcs_lock_qnode_t qnode = MCS_QNODE_INIT;
  mcs_lock_lock(&zombie_context_queue_lock, &qnode);
  if((c = TAILQ_FIRST(&zombie_context_queue)) != NULL) {
    TAILQ_REMOVE(&zombie_context_queue, c, link);
    mcs_lock_unlock(&zombie_context_queue_lock, &qnode);
    if(c->stack.size != stack_size) {
      assert(c->stack.bottom);
      free(c->stack.bottom);
      c->stack.size = stack_size;
      c->stack.bottom = malloc(c->stack.size);
      assert(c->stack.bottom);
    }
    lithe_context_reinit((lithe_context_t *)c, 
      (start_routine_t)&start_routine_wrapper, c);
  }
  else {
    mcs_lock_unlock(&zombie_context_queue_lock, &qnode);
  }
  return (libgomp_lithe_context_t*)c;
}

static libgomp_lithe_context_t *create_context(size_t stack_size)
{
  /* Create a new lithe context and initialize it from scratch */
  libgomp_lithe_context_t *c = 
    (libgomp_lithe_context_t*)malloc(sizeof(libgomp_lithe_context_t));
  assert(c);

  c->context.stack.size = stack_size;
  c->context.stack.bottom = malloc(c->context.stack.size);
  assert(c->context.stack.bottom);

  lithe_context_init((lithe_context_t *)c, 
    (start_routine_t)&start_routine_wrapper, c);

  return c;
}

static void destroy_context(libgomp_lithe_context_t *c)
{
  assert(c);
  /* If we are supposed to zobmify this context, add it to the zombie queue */
  if(c->make_zombie) {
    mcs_lock_qnode_t qnode = MCS_QNODE_INIT;
    mcs_lock_lock(&zombie_context_queue_lock, &qnode);
      TAILQ_INSERT_TAIL(&zombie_context_queue, &c->context, link);
    mcs_lock_unlock(&zombie_context_queue_lock, &qnode);
  }
  /* Otherwise, destroy it completely */
  else {
    lithe_context_cleanup(&c->context);
    assert(c->context.stack.bottom);
    free(c->context.stack.bottom);
    free(c);
  }
}
  
static void unlock_mcs_lock(void *arg) {
  struct lock_data {
    mcs_lock_t *lock;
    mcs_lock_qnode_t *qnode;
  } *real_lock = (struct lock_data*)arg;
  mcs_lock_unlock(real_lock->lock, real_lock->qnode);
}

static void schedule_context(lithe_context_t *context)
{
  libgomp_lithe_sched_t *sched = (libgomp_lithe_sched_t*)lithe_sched_current();
  mcs_lock_qnode_t qnode = MCS_QNODE_INIT;
  mcs_lock_lock(&sched->qlock, &qnode);
    TAILQ_INSERT_TAIL(&sched->context_queue, context, link);
    maybe_request_harts(1);
  mcs_lock_unlock(&sched->qlock, &qnode);
}

void libgomp_lithe_sched_ctor(libgomp_lithe_sched_t* sched)
{
  sched->sched.funcs = &libgomp_lithe_sched_funcs;
  sched->sched.main_context = malloc(sizeof(libgomp_lithe_context_t));
  ((libgomp_lithe_context_t*)(sched->sched.main_context))->completed = false;
  sched->num_contexts = 1;
  lithe_mutex_init(&sched->mutex, NULL);
  lithe_condvar_init(&sched->condvar);
  mcs_lock_init(&sched->qlock);
  TAILQ_INIT(&sched->context_queue);
  TAILQ_INIT(&sched->child_sched_queue);
}

void libgomp_lithe_sched_dtor(libgomp_lithe_sched_t* sched)
{
  free(sched->sched.main_context);
}

void libgomp_lithe_setstacksize(size_t stack_size)
{
  __context_stack_size = stack_size;
}

void libgomp_lithe_context_create(libgomp_lithe_context_t **__context,
  void (*start_routine)(void*), void *arg)
{
  libgomp_lithe_context_t *context;
  if((context = maybe_recycle_context(__context_stack_size)) == NULL) {
    context = create_context(__context_stack_size);
    context->make_zombie = true;
  }
  context->completed = false;
  context->start_routine = start_routine;
  context->arg = arg;
  *__context = context;

  libgomp_lithe_sched_t *sched = (libgomp_lithe_sched_t*)lithe_sched_current();
  __sync_fetch_and_add(&sched->num_contexts, 1);

  schedule_context(&context->context);
}

void libgomp_lithe_context_exit()
{
  libgomp_lithe_context_t *self = (libgomp_lithe_context_t*)lithe_context_self();
  self->make_zombie = false;
  lithe_context_exit();
}

void libgomp_lithe_context_rebind_sched(libgomp_lithe_context_t *c,
                                        libgomp_lithe_sched_t *s)
{
  lithe_context_reassociate(&c->context, &s->sched);
  __sync_fetch_and_add(&s->num_contexts, 1);
}

void libgomp_lithe_context_signal_completed()
{
  libgomp_lithe_context_t *self = (libgomp_lithe_context_t*)lithe_context_self();
  self->completed = true;
}

static void block_main_context(lithe_context_t *context, void *arg) {
  libgomp_lithe_context_t *self = (libgomp_lithe_context_t*)context;
  self->completed = true;
}

void libgomp_lithe_sched_join_completed()
{
  lithe_context_block(block_main_context, NULL);
}

static int hart_request(lithe_sched_t *__this, lithe_sched_t *child, int k)
{
  /* Find the child scheduler associated in our queue, and update the number
   * of harts it has requested */
  libgomp_lithe_sched_t *sched = (libgomp_lithe_sched_t *)__this;
  mcs_lock_qnode_t qnode = MCS_QNODE_INIT;
  mcs_lock_lock(&sched->qlock, &qnode);
    libgomp_lithe_child_sched_t *s = TAILQ_FIRST(&sched->child_sched_queue);
    while(s != NULL) { 
      if(s->sched == child) {
        s->requested_harts += k;
        break;
      }
      s = TAILQ_NEXT(s, link);
    }
    int ret = maybe_request_harts(k);
  mcs_lock_unlock(&sched->qlock, &qnode);
  return ret;
}

static void child_enter(lithe_sched_t *__this, lithe_sched_t *child)
{
  /* Add this child to our queue of child schedulers */
  libgomp_lithe_sched_t *sched = (libgomp_lithe_sched_t *)__this;
  libgomp_lithe_child_sched_t *child_wrapper = 
    (libgomp_lithe_child_sched_t*)malloc(sizeof(libgomp_lithe_child_sched_t));
  child_wrapper->sched = child;
  child_wrapper->requested_harts = 0;
  mcs_lock_qnode_t qnode = MCS_QNODE_INIT;
  mcs_lock_lock(&sched->qlock, &qnode);
    TAILQ_INSERT_TAIL(&sched->child_sched_queue, child_wrapper, link);
  mcs_lock_unlock(&sched->qlock, &qnode);
}

static void child_exit(lithe_sched_t *__this, lithe_sched_t *child)
{
  /* Cycle through our child schedulers and find the one corresponding to
   * this child and free it. */
  libgomp_lithe_sched_t *sched = (libgomp_lithe_sched_t *)__this;
  mcs_lock_qnode_t qnode = MCS_QNODE_INIT;
  mcs_lock_lock(&sched->qlock, &qnode);
    libgomp_lithe_child_sched_t *s,*n;
    s = TAILQ_FIRST(&sched->child_sched_queue); 
    while(s != NULL) { 
      n = TAILQ_NEXT(s, link);
      if(s->sched == child) {
        TAILQ_REMOVE(&sched->child_sched_queue, s, link);
        free(s);
        break;
      }
      s = n;
    }
  mcs_lock_unlock(&sched->qlock, &qnode);
}

static void hart_return(lithe_sched_t *__this, lithe_sched_t *child)
{
  /* Just call hart_enter() as that is where all of our logic for figuring
   * out what to do with a newly granted hart is. */
  assert(child);
  hart_enter(__this);
}

static void hart_enter(lithe_sched_t *__this)
{
  libgomp_lithe_sched_t *sched = (libgomp_lithe_sched_t *)__this;
  mcs_lock_qnode_t qnode = MCS_QNODE_INIT;
  mcs_lock_lock(&sched->qlock, &qnode);
    /* If we have child schedulers that have requested harts, prioritize them
     * access to this hart before ourselves */
    libgomp_lithe_child_sched_t *s = TAILQ_FIRST(&sched->child_sched_queue);
    while(s != NULL) { 
      if(s->requested_harts > 0) {
        struct {
          mcs_lock_t *lock;
          mcs_lock_qnode_t *qnode;
        } real_lock = {&sched->qlock, &qnode};
        s->requested_harts--;
        lithe_hart_grant(s->sched, unlock_mcs_lock, (void*)&real_lock);
        assert(0);
      }
      s = TAILQ_NEXT(s, link);
    }

    /* If we ever make it here, we have no child schedulers that have
     * requested harts, so just find one of our own contexts to run. */
    lithe_context_t *context = TAILQ_FIRST(&sched->context_queue);
    if(context != NULL) {
      TAILQ_REMOVE(&sched->context_queue, context, link);
    } 
  mcs_lock_unlock(&sched->qlock, &qnode);

  /* If there are no contexts to run, we can safely yield this hart */
  if(context == NULL)
    lithe_hart_yield();
  /* Otherwise, run the context that we found */
  else
    lithe_context_run((lithe_context_t *)context);
  assert(0);
}
  
static void context_block(lithe_sched_t *__this, lithe_context_t *__context)
{
  libgomp_lithe_sched_t *sched = (libgomp_lithe_sched_t*)__this;
  libgomp_lithe_context_t *context = (libgomp_lithe_context_t*)__context;

  if(context->completed) {
    if(__sync_add_and_fetch(&sched->num_contexts, -1) == 0) {
      lithe_context_unblock(sched->sched.main_context);
    }
  }
}
  
static void context_unblock(lithe_sched_t *__this, lithe_context_t *__context)
{
  schedule_context(__context);
}

static void context_yield(lithe_sched_t *__this, lithe_context_t *context)
{
  schedule_context(context);
}

static void context_exit(lithe_sched_t *__this, lithe_context_t *context)
{
  destroy_context((libgomp_lithe_context_t*)context);
}

