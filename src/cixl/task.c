#include <errno.h>
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
  pthread_cond_init(&t->cond, NULL);
  cx_vec_init(&t->libs, sizeof(struct cx_lib *));
  cx_vec_init(&t->scopes, sizeof(struct cx_scope *));
  cx_vec_init(&t->calls, sizeof(struct cx_call));
  cx_ls_init(&t->queue);
  cx_copy(&t->action, action);
  return t;
}

struct cx_task *cx_task_deinit(struct cx_task *t) {
  cx_box_deinit(&t->action);

  if (t->state != CX_TASK_NEW) {
    pthread_join(t->thread, NULL);
  }

  if (t->state == CX_TASK_RUN) {
    cx_do_vec(&t->scopes, struct cx_scope *, s) { cx_scope_deref(*s); }
    cx_do_vec(&t->calls, struct cx_call, c) { cx_call_deinit(c); }
  }
  
  pthread_cond_destroy(&t->cond);
  cx_vec_deinit(&t->libs);
  cx_vec_deinit(&t->scopes);
  cx_vec_deinit(&t->calls);
  return t;
}

bool cx_task_resched(struct cx_task *t, struct cx_scope *scope) {
  cx_test(t->state != CX_TASK_DONE);
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

  pthread_cond_signal(&t->sched->cond);  
  pthread_cond_wait(&t->cond, &t->sched->mutex);
  return true;
}

static void *on_start(void *data) {
  struct cx_scope *scope = data;
  struct cx *cx = scope->cx;
  struct cx_task *t = cx->task;
  t->state = CX_TASK_RUN;
  pthread_mutex_lock(&t->sched->mutex);
  cx_call(&t->action, scope);
  cx->task = t->prev_task;
  cx->bin = t->prev_bin;
  cx->pc = t->prev_pc;
  while (cx->libs.count > t->prev_nlibs) { cx_pop_lib(cx); }
  while (cx->scopes.count > t->prev_nscopes) { cx_pop_scope(cx, false); }
  while (cx->ncalls > t->prev_ncalls) { cx_test(cx_pop_call(cx)); }
  t->state = CX_TASK_DONE;
  pthread_cond_signal(&t->sched->cond);
  pthread_mutex_unlock(&t->sched->mutex);
  return NULL;
}

bool cx_task_run(struct cx_task *t, struct cx_scope *scope) {
  struct cx *cx = scope->cx;
  t->prev_task = cx->task;
  cx->task = t;
  t->prev_bin = cx->bin;
  cx->bin = t->bin;
  t->prev_pc = cx->pc;
  cx->pc = t->pc;
  t->prev_nlibs = cx->libs.count;
  t->prev_nscopes = cx->scopes.count;
  t->prev_ncalls = cx->ncalls;

  switch (t->state) {
  case CX_TASK_NEW: {
    pthread_attr_t a;
    pthread_attr_init(&a);
    pthread_attr_setstacksize(&a, CX_TASK_STACK_SIZE);
    pthread_create(&t->thread, &a, on_start, scope);
    pthread_attr_destroy(&a);
    break;
  }
  case CX_TASK_RUN: {
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
    
    pthread_cond_signal(&t->cond);
    break;
  }
  default:
    cx_error(cx, cx->row, cx->col, "Invalid task run state: %d", t->state);
    return false;
  }

  pthread_cond_wait(&t->sched->cond, &t->sched->mutex);
  return true;
}
