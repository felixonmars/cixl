#include <stdbool.h>
#include <stdlib.h>

#include "cixl/box.h"
#include "cixl/cx.h"
#include "cixl/error.h"
#include "cixl/iter.h"
#include "cixl/malloc.h"
#include "cixl/stack.h"

struct cx_stack_iter {
  struct cx_iter iter;
  struct cx_stack *stack;
  ssize_t i, end;
  int delta;
};

static bool stack_next(struct cx_iter *iter,
		       struct cx_box *out,
		       struct cx_scope *scope) {
  struct cx_stack_iter *it = cx_baseof(iter, struct cx_stack_iter, iter);

  if (it->i != it->end) {
    cx_copy(out, cx_vec_get(&it->stack->imp, it->i));
    it->i += it->delta;
    return true;
  }

  iter->done = true;
  return false;
}

static void *stack_deinit(struct cx_iter *iter) {
  struct cx_stack_iter *it = cx_baseof(iter, struct cx_stack_iter, iter);
  cx_stack_deref(it->stack);
  return it;
}

static cx_iter_type(stack_iter, {
    type.next = stack_next;
    type.deinit = stack_deinit;
  });

struct cx_iter *cx_stack_iter_new(struct cx_stack *stack,
				  ssize_t start, size_t end,
				  int delta) {
  struct cx_stack_iter *it = malloc(sizeof(struct cx_stack_iter));
  cx_iter_init(&it->iter, stack_iter());
  it->stack = cx_stack_ref(stack);
  it->i = start;
  it->end = end;
  it->delta = delta;
  return &it->iter;
}

struct cx_stack *cx_stack_new(struct cx *cx) {
  struct cx_stack *v = cx_malloc(&cx->stack_alloc);
  v->cx = cx;
  cx_vec_init(&v->imp, sizeof(struct cx_box));
  v->imp.alloc = &cx->stack_items_alloc;
  v->nrefs = 1;
  return v;
}

struct cx_stack *cx_stack_ref(struct cx_stack *stack) {
  stack->nrefs++;
  return stack;
}

void cx_stack_deref(struct cx_stack *stack) {
  cx_test(stack->nrefs);
  stack->nrefs--;

  if (!stack->nrefs) {
    cx_do_vec(&stack->imp, struct cx_box, b) { cx_box_deinit(b); }
    cx_vec_deinit(&stack->imp);
    cx_free(&stack->cx->stack_alloc, stack);
  }
}

void cx_stack_dump(struct cx_vec *imp, FILE *out) {
  fputc('[', out);
  char sep = 0;
  
  cx_do_vec(imp, struct cx_box, b) {
    if (sep) { fputc(sep, out); }
    cx_dump(b, out);
    sep = ' ';
  }

  fputc(']', out);
}

static void new_imp(struct cx_box *out) {
  out->as_ptr = cx_stack_new(out->type->lib->cx);
}

static bool equid_imp(struct cx_box *x, struct cx_box *y) {
  return x->as_ptr == y->as_ptr;
}

static bool eqval_imp(struct cx_box *x, struct cx_box *y) {
  struct cx_stack *xv = x->as_ptr, *yv = y->as_ptr;
  if (xv->imp.count != yv->imp.count) { return false; }
  
  for (size_t i = 0; i < xv->imp.count; i++) {
    if (!cx_eqval(cx_vec_get(&xv->imp, i), cx_vec_get(&yv->imp, i))) {
      return false;
    }
  }
  
  return true;
}

static enum cx_cmp cmp_imp(const struct cx_box *x, const struct cx_box *y) {
  struct cx_stack *xv = x->as_ptr, *yv = y->as_ptr;
  struct cx_box *xe = cx_vec_end(&xv->imp), *ye = cx_vec_end(&yv->imp);
  
  for (struct cx_box *xp = cx_vec_start(&xv->imp), *yp = cx_vec_start(&yv->imp);
       xp != xe && yp != ye;
       xp++, yp++) {
    enum cx_cmp res = cx_cmp(xp, yp);
    if (res != CX_CMP_EQ) { return res; }
  }

  if (xv->imp.count < yv->imp.count) { return CX_CMP_LT; }
  return (xv->imp.count > yv->imp.count) ? CX_CMP_GT : CX_CMP_EQ;
}

static bool ok_imp(struct cx_box *b) {
  struct cx_stack *v = b->as_ptr;
  return v->imp.count;
}

static void copy_imp(struct cx_box *dst, const struct cx_box *src) {
  dst->as_ptr = cx_stack_ref(src->as_ptr);
}

static void clone_imp(struct cx_box *dst, struct cx_box *src) {
  struct cx *cx = src->type->lib->cx;
  struct cx_stack *src_stack = src->as_ptr, *dst_stack = cx_stack_new(cx);
  dst->as_ptr = dst_stack;

  cx_do_vec(&src_stack->imp, struct cx_box, v) {
    cx_clone(cx_vec_push(&dst_stack->imp), v);
  }
}

static void iter_imp(struct cx_box *in, struct cx_box *out) {
  struct cx *cx = in->type->lib->cx;
  struct cx_stack *s = in->as_ptr;
  
  cx_box_init(out, cx_type_get(cx->iter_type,
			       cx_type_arg(cx_subtype(in->type, cx->stack_type),
					   0)))->as_iter =
    cx_stack_iter_new(s, 0, s->imp.count, 1);
}

static bool sink_imp(struct cx_box *dst, struct cx_box *v) {
  struct cx_stack *s = dst->as_ptr;
  cx_copy(cx_vec_push(&s->imp), v);
  return true;
}

static void write_imp(struct cx_box *b, FILE *out) {
  struct cx_stack *v = b->as_ptr;

  fputs("[", out);
  char sep = 0;
  
  cx_do_vec(&v->imp, struct cx_box, b) {
    if (sep) { fputc(sep, out); }
    cx_write(b, out);
    sep = ' ';
  }

  fputs("]", out);
}

static void dump_imp(struct cx_box *b, FILE *out) {
  struct cx_stack *v = b->as_ptr;
  cx_stack_dump(&v->imp, out);
}

static void print_imp(struct cx_box *b, FILE *out) {
  struct cx_stack *stack = b->as_ptr;
  cx_do_vec(&stack->imp, struct cx_box, v) {
    cx_print(v, out);
  }
}

static bool emit_imp(struct cx_box *v, const char *exp, FILE *out) {
  struct cx *cx = v->type->lib->cx;
  struct cx_sym s_var = cx_gsym(cx, "s");
  
  fprintf(out,
	  "struct cx_stack *%s = cx_stack_new(cx);\n"
	  "cx_box_init(%s, cx_get_type(cx, \"%s\", false))->as_ptr = %s;\n",
	  s_var.id, exp, v->type->id, s_var.id);

  struct cx_sym v_var = cx_gsym(cx, "v");
  fprintf(out, "struct cx_box *%s = NULL;\n", v_var.id);
  
  struct cx_stack *s = v->as_ptr;
  cx_do_vec(&s->imp, struct cx_box, i) {
    fprintf(out, "%s = cx_vec_push(&%s->imp);\n", v_var.id, s_var.id);
    if (!cx_box_emit(i, v_var.id, out)) { return false; }
  }
  
  return true;
}

static void deinit_imp(struct cx_box *v) {
  cx_stack_deref(v->as_ptr);
}

static bool type_init_imp(struct cx_type *t, int nargs, struct cx_type *args[]) {
  struct cx *cx = t->lib->cx;
  cx_derive(t, cx_test(cx_type_get(cx->seq_type, args[0])));
  cx_derive(t, cx_test(cx_type_get(cx->sink_type, args[0])));
  return true;
}

struct cx_type *cx_init_stack_type(struct cx_lib *lib) {
  struct cx *cx = lib->cx;
  struct cx_type *t = cx_add_type(lib, "Stack",
				  cx->cmp_type, cx->seq_type, cx->sink_type);
  
  cx_type_push_args(t, cx->opt_type);

  t->new = new_imp;
  t->eqval = eqval_imp;
  t->equid = equid_imp;
  t->cmp = cmp_imp;
  t->ok = ok_imp;
  t->copy = copy_imp;
  t->clone = clone_imp;
  t->iter = iter_imp;
  t->sink = sink_imp;
  t->write = write_imp;
  t->dump = dump_imp;
  t->print = print_imp;
  t->emit = emit_imp;
  t->deinit = deinit_imp;
  
  t->type_init = type_init_imp;
  return t;
}
