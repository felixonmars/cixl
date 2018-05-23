#include <errno.h>
#include <stdatomic.h>
#include <stdlib.h>

#include "cixl/cx.h"
#include "cixl/error.h"
#include "cixl/iter.h"
#include "cixl/lib.h"
#include "cixl/sched.h"
#include "cixl/scope.h"
#include "cixl/task.h"
#include "cixl/type.h"

struct cx_sched *cx_sched_new(struct cx *cx) {
  struct cx_sched *s = malloc(sizeof(struct cx_sched));
  s->cx = cx;
  s->nrescheds = 0;
  s->nready = 0;
  s->nrefs = 1;
  cx_ls_init(&s->ready_q);
  cx_ls_init(&s->done_q);

  if (sem_init(&s->go, false, 0) != 0) {
    cx_error(cx, cx->row, cx->col, "Failed initializing semaphore: %d", errno);
  }

  if (sem_init(&s->done, false, 0) != 0) {
    cx_error(cx, cx->row, cx->col, "Failed initializing semaphore: %d", errno);
  }

  if (pthread_mutex_init(&s->q_lock, NULL) != 0) {
    cx_error(cx, cx->row, cx->col, "Failed initializing mutex: %d", errno);
  }

  return s;
}

struct cx_sched *cx_sched_ref(struct cx_sched *s) {
  s->nrefs++;
  return s;
}

void cx_sched_deref(struct cx_sched *s) {
  cx_test(s->nrefs);
  s->nrefs--;
  
  if (!s->nrefs) {
    if (sem_destroy(&s->go) != 0) {
      cx_error(s->cx, s->cx->row, s->cx->col,
	       "Failed destroying semaphore: %d", errno);
    }

    if (sem_destroy(&s->done) != 0) {
      cx_error(s->cx, s->cx->row, s->cx->col,
	       "Failed destroying semaphore: %d", errno);
    }

    if (pthread_mutex_destroy(&s->q_lock) != 0) {
      cx_error(s->cx, s->cx->row, s->cx->col, "Failed destroying mutex: %d", errno);
    }
  
    cx_do_ls(&s->ready_q, tp) {
      free(cx_task_deinit(cx_baseof(tp, struct cx_task, q)));
    }

    cx_do_ls(&s->done_q, tp) {
      free(cx_task_deinit(cx_baseof(tp, struct cx_task, q)));
    }

    free(s);
  }
}

bool cx_sched_push(struct cx_sched *s, struct cx_box *action) {
  struct cx_task *t = cx_task_init(malloc(sizeof(struct cx_task)), s, action);

  if (pthread_mutex_lock(&s->q_lock) != 0) {
    cx_error(s->cx, s->cx->row, s->cx->col, "Failed locking: %d", errno);
    return false;
  }

  cx_ls_prepend(&s->ready_q, &t->q);
  bool ok = cx_task_start(t);
  if (!ok) { goto exit; }
  while (atomic_load(&t->state) == CX_TASK_NEW) { sched_yield(); }
 exit:
  if (pthread_mutex_unlock(&s->q_lock) != 0) {
    cx_error(s->cx, s->cx->row, s->cx->col, "Failed locking: %d", errno);
    return false;
  }

  return ok;
}

bool cx_sched_run(struct cx_sched *s, struct cx_scope *scope) {
  if (sem_post(&s->go) != 0) {
    cx_error(s->cx, s->cx->row, s->cx->col, "Failed posting: %d", errno);
    return false;
  }
  
  while (atomic_load(&s->nready)) {
    if (sem_wait(&s->done) != 0) {
      cx_error(s->cx, s->cx->row, s->cx->col, "Failed waiting: %d", errno);
      return false;
    }
    
    if (pthread_mutex_lock(&s->q_lock) != 0) {
      cx_error(s->cx, s->cx->row, s->cx->col, "Failed locking: %d", errno);
      return false;
    }

    struct cx_task *t = cx_baseof(s->done_q.next, struct cx_task, q);
    cx_ls_delete(&t->q);
    free(cx_task_deinit(t));

    if (pthread_mutex_unlock(&s->q_lock) != 0) {
      cx_error(s->cx, s->cx->row, s->cx->col, "Failed unlocking: %d", errno);
      return false;
    }
  }

  while (s->done_q.next != &s->done_q) {
    struct cx_task *t = cx_baseof(s->done_q.next, struct cx_task, q);
    cx_ls_delete(&t->q);
    free(cx_task_deinit(t));
  }

  return true;
}

static void new_imp(struct cx_box *out) {
  out->as_sched = cx_sched_new(out->type->lib->cx);
}

static bool equid_imp(struct cx_box *x, struct cx_box *y) {
  return x->as_sched == y->as_sched;
}

static void copy_imp(struct cx_box *dst, const struct cx_box *src) {
  dst->as_sched = cx_sched_ref(src->as_sched);
}

static bool sink_imp(struct cx_box *dst, struct cx_box *v) {
  return cx_sched_push(dst->as_sched, v);
}

static void dump_imp(struct cx_box *v, FILE *out) {
  fprintf(out, "Sched(%p)", v->as_sched);
}

static void deinit_imp(struct cx_box *v) {
  cx_sched_deref(v->as_sched);
}

struct cx_type *cx_init_sched_type(struct cx_lib *lib) {
  struct cx_type *t = cx_add_type(lib, "Sched", lib->cx->sink_type);
  t->new = new_imp;
  t->equid = equid_imp;
  t->copy = copy_imp;
  t->sink = sink_imp;
  t->dump = dump_imp;
  t->deinit = deinit_imp;
  return t;
}
