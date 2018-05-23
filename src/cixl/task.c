#include <errno.h>
#include <stdatomic.h>
#include <string.h>

#include "cixl/cx.h"
#include "cixl/error.h"
#include "cixl/sched.h"
#include "cixl/scope.h"
#include "cixl/task.h"

struct cx_task *cx_task_init(struct cx_task *t,
			     struct cx_sched *sched,
			     struct cx_box *action) {
  t->sched = sched;
  t->state = CX_TASK_NEW;
  t->prev_task = NULL;
  t->prev_bin = t->bin = NULL;
  t->prev_pc = t->pc = -1;
  t->prev_nlibs = t->prev_nscopes = t->prev_ncalls = -1;
  cx_vec_init(&t->libs, sizeof(struct cx_lib *));
  cx_vec_init(&t->scopes, sizeof(struct cx_scope *));
  cx_vec_init(&t->calls, sizeof(struct cx_call));
  cx_ls_init(&t->q);
  cx_copy(&t->action, action);
  return t;
}

struct cx_task *cx_task_deinit(struct cx_task *t) {
  struct cx *cx = cx;
  
  if (pthread_join(t->thread, NULL) != 0) {
    cx_error(cx, cx->row, cx->col, "Failed joining thread: %d", errno);
  }
  
  cx_box_deinit(&t->action);
  cx_do_vec(&t->scopes, struct cx_scope *, s) { cx_scope_deref(*s); }
  cx_do_vec(&t->calls, struct cx_call, c) { cx_call_deinit(c); }
  
  cx_vec_deinit(&t->libs);
  cx_vec_deinit(&t->scopes);
  cx_vec_deinit(&t->calls);
  return t;
}

static void before_run(struct cx_task *t, struct cx *cx) {
  t->prev_task = cx->task;
  cx->task = t;
  t->prev_bin = cx->bin;
  cx->bin = t->bin;
  t->prev_pc = cx->pc;
  cx->pc = t->pc;
  t->prev_nlibs = cx->libs.count;
  t->prev_nscopes = cx->scopes.count;
  t->prev_ncalls = cx->ncalls;
}

static void before_resume(struct cx_task *t, struct cx *cx) {
  before_run(t, cx);
  
  if (t->libs.count) {
    cx_vec_grow(&cx->libs, cx->libs.count+t->libs.count);
    
    memcpy(cx_vec_end(&cx->libs),
	   t->libs.items,
	   sizeof(struct cx_lib *) * t->libs.count);
    
    cx->libs.count += t->libs.count;
    cx->lib = cx_vec_peek(&cx->libs, 0);
    cx_vec_clear(&t->libs);
  }

  if (t->scopes.count) {
    cx_vec_grow(&cx->scopes, cx->scopes.count+t->scopes.count);
    
    memcpy(cx_vec_end(&cx->scopes),
	   t->scopes.items,
	   sizeof(struct cx_scope *) * t->scopes.count);
    
    cx->scopes.count += t->scopes.count;
    cx->scope = cx_vec_peek(&cx->scopes, 0);
    cx_vec_clear(&t->scopes);
  }

  if (t->calls.count) {
    memcpy(cx->calls+cx->ncalls,
	   t->calls.items,
	   sizeof(struct cx_call) * t->calls.count);
    
    cx->ncalls += t->calls.count;
    cx_vec_clear(&t->calls);
  }
}

bool cx_task_resched(struct cx_task *t, struct cx_scope *scope) {
  atomic_fetch_add(&t->sched->nrescheds, 1);
  struct cx *cx = scope->cx;
  cx->task = t->prev_task;
  t->bin = cx->bin;
  cx->bin = t->prev_bin;
  t->pc = cx->pc;
  cx->pc = t->prev_pc;

  ssize_t nlibs = cx->libs.count - t->prev_nlibs;
  
  if (nlibs) {
    cx_vec_grow(&t->libs, nlibs);

    memcpy(t->libs.items,
	   cx_vec_get(&cx->libs, t->prev_nlibs),
	   sizeof(struct cx_lib *) * nlibs);

    t->libs.count = nlibs;
    cx->libs.count -= nlibs;
    cx->lib -= nlibs;
  }

  ssize_t nscopes = cx->scopes.count - t->prev_nscopes;
  
  if (nscopes) {
    cx_vec_grow(&t->scopes, nscopes);

    memcpy(t->scopes.items,
	   cx_vec_get(&cx->scopes, t->prev_nscopes),
	   sizeof(struct cx_scope *) * nscopes);

    t->scopes.count = nscopes;
    cx->scopes.count -= nscopes;
    cx->scope -= nscopes;
  }

  ssize_t ncalls = cx->ncalls - t->prev_ncalls;
  
  if (ncalls) {
    cx_vec_grow(&t->calls, ncalls);
    memcpy(t->calls.items, cx->calls+t->prev_ncalls, sizeof(struct cx_call) * ncalls);
    t->calls.count = ncalls;
    cx->ncalls -= ncalls;
  }

  if (atomic_load(&t->sched->nready) > 1) {
    size_t nrescheds = t->sched->nrescheds;
    
    if (sem_post(&t->sched->go) != 0) {
      cx_error(cx, cx->row, cx->col, "Failed posting: %d", errno);
    }

    while (atomic_load(&t->sched->nready) > 1 &&
	   atomic_load(&t->sched->nrescheds) == nrescheds) {
      sched_yield();
    }
    
    if (sem_wait(&t->sched->go) != 0) {
      cx_error(cx, cx->row, cx->col, "Failed waiting: %d", errno);
    }
  }

  before_resume(t, cx);
  return true;
}

static void *on_start(void *data) {
  struct cx_task *t = data;
  struct cx *cx = t->sched->cx;
  atomic_store(&t->state, CX_TASK_RUN);
  atomic_fetch_add(&t->sched->nready, 1);
  
  if (sem_wait(&t->sched->go) != 0) {
    cx_error(cx, cx->row, cx->col, "Failed waiting: %d", errno);
    return NULL;
  }
  
  struct cx_scope *scope = cx_scope(cx, 0);
  before_run(t, scope->cx);
  cx_call(&t->action, scope);

  cx->task = t->prev_task;
  cx->bin = t->prev_bin;
  cx->pc = t->prev_pc;
  while (cx->libs.count > t->prev_nlibs) { cx_pop_lib(cx); }
  while (cx->scopes.count > t->prev_nscopes) { cx_pop_scope(cx, false); }
  while (cx->ncalls > t->prev_ncalls) { cx_test(cx_pop_call(cx)); }

  if (atomic_fetch_sub(&t->sched->nready, 1) > 1 &&
      sem_post(&t->sched->go) != 0) {
    cx_error(cx, cx->row, cx->col, "Failed posting: %d", errno);
  }
  
  return NULL;
}

bool cx_task_start(struct cx_task *t) {  
  pthread_attr_t a;
  pthread_attr_init(&a);
  pthread_attr_setstacksize(&a, CX_TASK_STACK_SIZE);
  pthread_attr_setschedpolicy(&a, SCHED_RR);
  bool ok = pthread_create(&t->thread, &a, on_start, t) == 0;

  if (!ok) {
    struct cx *cx = t->sched->cx;
    cx_error(cx, cx->row, cx->col, "Failed creating thread: %d", errno);
  }

  pthread_attr_destroy(&a);
  return ok;
}
