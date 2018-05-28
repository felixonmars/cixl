#include <errno.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

#include "cixl/bin.h"
#include "cixl/box.h"
#include "cixl/call.h"
#include "cixl/cx.h"
#include "cixl/error.h"
#include "cixl/coro.h"
#include "cixl/iter.h"
#include "cixl/op.h"
#include "cixl/scope.h"
#include "cixl/tok.h"

struct cx_coro *cx_coro_new(struct cx *cx, struct cx_box *action) {
  struct cx_coro *c = malloc(sizeof(struct cx_coro));
  c->cx = cx;
  c->state = CX_CORO_NEW;
  c->nrefs = 1;

  if (sem_init(&c->on_call, false, 0) != 0) {
    cx_error(cx, cx->row, cx->col, "Failed initializing call: %d", errno);
  }

  cx_cont_init(&c->cont, cx);
  cx_copy(&c->action, action);
  return c;
}

struct cx_coro *cx_coro_ref(struct cx_coro *c) {
  c->nrefs++;
  return c;
}

struct cx_coro *cx_coro_deinit(struct cx_coro *c) {
  struct cx *cx = c->cx;

  if (c->state != CX_CORO_NEW) {
    if (c->state != CX_CORO_DONE && pthread_join(c->thread, NULL) != 0) {
      cx_error(cx, cx->row, cx->col, "Failed cancelling thread: %d", errno);
    }
    
    if (pthread_join(c->thread, NULL) != 0) {
      cx_error(cx, cx->row, cx->col, "Failed joining thread: %d", errno);
    }
  }
  
  if (c->state != CX_CORO_CANCEL) {
    cx_box_deinit(&c->action);
    cx_cont_deinit(&c->cont);
  }

  if (sem_destroy(&c->on_call) != 0) {
    cx_error(cx, cx->row, cx->col, "Failed destroying call: %d", errno);
  }
  
  return c;
}

void cx_coro_deref(struct cx_coro *c) {
  cx_test(c->nrefs);
  c->nrefs--;
  if (!c->nrefs) { free(cx_coro_deinit(c)); }
}

static void suspend_stack(struct cx_scope *src) {
  struct cx_scope *dst = cx_scope(src->cx, 0);
  
  if (dst != src) {
    cx_vec_grow(&dst->stack, dst->stack.count+src->stack.count);
    
    memcpy(cx_vec_end(&dst->stack),
	   src->stack.items,
	   sizeof(struct cx_box) * src->stack.count);
    
    dst->stack.count += src->stack.count;
    cx_vec_clear(&src->stack);
  }
}

static void *on_start(void *data) {
  struct cx_coro *c = data;
  cx_call(&c->action, cx_scope(c->cx, 0));
  atomic_store(&c->state, CX_CORO_DONE);
  struct cx_scope *src = cx_scope(c->cx, 0);
  cx_cont_finish(&c->cont);
  if (src->stack.count) { suspend_stack(src); }
  return NULL;
}

bool cx_coro_call(struct cx_coro *c, struct cx_scope *scope) {
  struct cx *cx = c->cx;
  bool ok = false;
  
  switch (c->state) {
  case CX_CORO_NEW: {
    cx_cont_reset(&c->cont);
    cx->coro = c;
    c->state = CX_CORO_RESUME;

    pthread_attr_t a;
    pthread_attr_init(&a);
    pthread_attr_setstacksize(&a, CX_CORO_STACK_SIZE);  
    bool ok = pthread_create(&c->thread, &a, on_start, c) == 0;
    pthread_attr_destroy(&a);
    
    if (!ok) {
      cx_error(cx, cx->row, cx->col, "Failed creating thread: %d", errno);
      goto exit;
    }

    while (atomic_load(&c->state) == CX_CORO_RESUME) { sched_yield(); }
    break;
  }
  case CX_CORO_SUSPEND: {
    cx_cont_resume(&c->cont);
    cx->coro = c;
    c->state = CX_CORO_RESUME;
    
    if (sem_post(&c->on_call) != 0) {
      cx_error(cx, cx->row, cx->col, "Failed posting run: %d", errno);
      goto exit;
    }

    while (atomic_load(&c->state) == CX_CORO_RESUME) { sched_yield(); }
    break;
  }
  case CX_CORO_RESUME:
    cx_error(cx, cx->row, cx->col, "Coro isn't suspended");
    goto exit;
  case CX_CORO_DONE:
    cx_error(cx, cx->row, cx->col, "Coro is done");
    goto exit;
  case CX_CORO_CANCEL:
    cx_error(cx, cx->row, cx->col, "Coro is cancelled");
    goto exit;
  }

  ok = true;
 exit:
  return ok;
}

bool cx_coro_reset(struct cx_coro *c) {
  if (c->state == CX_CORO_NEW) { return true; }
  struct cx *cx = c->cx;  

  if (c->state != CX_CORO_DONE) {
    if (pthread_cancel(c->thread) != 0) {
      cx_error(cx, cx->row, cx->col, "Failed cancelling thread: %d", errno);
      return false;
    }
  }

  if (pthread_join(c->thread, NULL) != 0) {
    cx_error(cx, cx->row, cx->col, "Failed joining thread: %d", errno);
    return false;
  }
  
  cx_cont_clear(&c->cont);
  c->state = CX_CORO_NEW;
  return true;
}

bool cx_coro_cancel(struct cx_coro *c) {
  if (c->state == CX_CORO_CANCEL) { return true; }
  struct cx *cx = c->cx;
  
  if (c->state != CX_CORO_NEW) {
    if (c->state != CX_CORO_DONE) {
      if (pthread_cancel(c->thread) != 0) {
	cx_error(cx, cx->row, cx->col, "Failed cancelling thread: %d", errno);
	return false;
      }
    }
    
    if (pthread_join(c->thread, NULL) != 0) {
      cx_error(cx, cx->row, cx->col, "Failed joining thread: %d", errno);
      return false;
    }
  }

  cx_box_deinit(&c->action);
  cx_cont_deinit(&c->cont);
  c->state = CX_CORO_CANCEL;
  return true;
}

bool cx_coro_suspend(struct cx_coro *c, struct cx_scope *scope) {
  struct cx *cx = c->cx;
  cx_cont_suspend(&c->cont);
  if (scope->stack.count) { suspend_stack(scope); }
  atomic_store(&c->state, CX_CORO_SUSPEND);

  if (sem_wait(&c->on_call) != 0) {
    cx_error(cx, cx->row, cx->col, "Failed waiting on run: %d", errno);
    return false;
  }

  return true;
}

struct coro_iter {
  struct cx_iter iter;
  struct cx_coro *src;
};

static bool coro_next(struct cx_iter *iter,
		      struct cx_box *out,
		      struct cx_scope *scope) {
  struct coro_iter *it = cx_baseof(iter, struct coro_iter, iter);
  
  if (it->src->state == CX_CORO_DONE) {
    iter->done = true;
    return false;
  }
  
  return true;
}

static void *coro_deinit(struct cx_iter *iter) {
  struct coro_iter *it = cx_baseof(iter, struct coro_iter, iter);
  cx_coro_deref(it->src);
  return it;
}

static cx_iter_type(coro_iter, {
    type.next = coro_next;
    type.deinit = coro_deinit;
  });

static struct cx_iter *coro_iter_new(struct cx_coro *src) {
  struct coro_iter *it = malloc(sizeof(struct coro_iter));
  cx_iter_init(&it->iter, coro_iter());
  it->src = cx_coro_ref(src);
  return &it->iter;
}

static bool equid_imp(struct cx_box *x, struct cx_box *y) {
  return x->as_ptr == y->as_ptr;
}

static bool call_imp(struct cx_box *v, struct cx_scope *scope) {
  struct cx_coro *c = cx_coro_ref(v->as_ptr);
  bool ok = cx_coro_call(c, scope);
  cx_coro_deref(c);
  return ok;
}

static void copy_imp(struct cx_box *dst, const struct cx_box *src) {
  dst->as_ptr = cx_coro_ref(src->as_ptr);
}

static void iter_imp(struct cx_box *in, struct cx_box *out) {
  struct cx *cx = in->type->lib->cx;
  cx_box_init(out, cx->iter_type)->as_iter = coro_iter_new(in->as_ptr);
}

static void dump_imp(struct cx_box *v, FILE *out) {
  fprintf(out, "Coro(%p)", v->as_ptr);
}

static void deinit_imp(struct cx_box *v) {
  cx_coro_deref(v->as_ptr);
}

struct cx_type *cx_init_coro_type(struct cx_lib *lib) {
  struct cx_type *t = cx_add_type(lib, "Coro", lib->cx->seq_type);
  t->equid = equid_imp;
  t->call = call_imp;
  t->copy = copy_imp;
  t->iter = iter_imp;
  t->dump = dump_imp;
  t->deinit = deinit_imp;
  return t;
}
