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
  s->ntasks = 0;
  s->nrefs = 1;
  cx_ls_init(&s->tasks);

  if (sem_init(&s->go, false, 0) != 0) {
    cx_error(cx, cx->row, cx->col, "Failed initializing: %d", errno);
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
      cx_error(s->cx, s->cx->row, s->cx->col, "Failed destroying: %d", errno);
    }
     
    cx_do_ls(&s->tasks, tp) {
      free(cx_task_deinit(cx_baseof(tp, struct cx_task, queue)));
    }
    
    free(s);
  }
}

bool cx_sched_push(struct cx_sched *s, struct cx_box *action) {
  struct cx_task *t = cx_task_init(malloc(sizeof(struct cx_task)), s, action);
  cx_ls_prepend(&s->tasks, &t->queue);
  if (!cx_task_start(t)) { return false; }
  while (atomic_load(&t->state) == CX_TASK_NEW) { sched_yield(); }
  return true;
}

bool cx_sched_run(struct cx_sched *s, struct cx_scope *scope) {
  if (sem_post(&s->go) != 0) {
    cx_error(s->cx, s->cx->row, s->cx->col, "Failed posting: %d", errno);
    return false;
  }
  
  while (atomic_load(&s->ntasks)) {
    struct cx_task *t = cx_baseof(s->tasks.next, struct cx_task, queue);
    cx_ls_delete(&t->queue);
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
