#include "cixl/box.h"
#include "cixl/cx.h"
#include "cixl/error.h"
#include "cixl/scope.h"
#include "cixl/type.h"

struct cx_box *cx_box_new(struct cx_type *type) {
  return cx_box_init(malloc(sizeof(struct cx_box)), type);
}

struct cx_box *cx_box_init(struct cx_box *box, struct cx_type *type) {
  box->type = cx_test(type);
  return box;
}

struct cx_box *cx_box_deinit(struct cx_box *box) {
  if (box->type->deinit) { box->type->deinit(box); }
  return box;
}

bool cx_box_emit(struct cx_box *box, const char *exp, FILE *out) {
  if (!box->type->emit) {
    struct cx *cx = box->type->lib->cx;
    cx_error(cx, cx->row, cx->col, "Emit not implemented: %s", box->type->id);
    return false;
  }
  
  return box->type->emit(box, exp, out);
}

bool cx_eqval(struct cx_box *x, struct cx_box *y) {
  return x->type->eqval ? x->type->eqval(x, y) : cx_equid(x, y);
}

bool cx_equid(struct cx_box *x, struct cx_box *y) {
  return cx_test(x->type->equid)(x, y);
}

enum cx_cmp cx_cmp(const struct cx_box *x, const struct cx_box *y) {
  return cx_test(x->type->cmp)(x, y);
}

bool cx_ok(struct cx_box *x) {
  return x->type->ok ? x->type->ok(x) : true;
}

bool cx_call(struct cx_box *box, struct cx_scope *scope) {
  if (!box->type->call) {
    cx_copy(cx_push(scope), box);
    return true;
  }
  
  return box->type->call(box, scope);
}

struct cx_box *cx_copy(struct cx_box *dst, const struct cx_box *src) {
  if (src->type->copy) {
    dst->type = src->type;
    src->type->copy(dst, src);
  } else {
    *dst = *src;
  }

  return dst;
}

struct cx_box *cx_clone(struct cx_box *dst, struct cx_box *src) {
  if (!src->type->clone) { return cx_copy(dst, src); }
  dst->type = src->type;
  src->type->clone(dst, src);
  return dst;
}

void cx_iter(struct cx_box *in, struct cx_box *out) {
  cx_test(in->type->iter)(in, out);
}

bool cx_sink(struct cx_box *dst, struct cx_box *v) {
  return cx_test(dst->type->sink)(dst, v);
}

bool cx_write(struct cx_box *box, FILE *out) {
  if (!box->type->write) {
    struct cx *cx = box->type->lib->cx;
    
    cx_error(cx, cx->row, cx->col,
	     "Write not implemented for type: %s",
	     box->type->id);

    return false;
  }
  
  box->type->write(box, out);
  return true;
}

void cx_dump(struct cx_box *box, FILE *out) {
  cx_test(box->type->dump)(box, out);
}

void cx_print(struct cx_box *box, FILE *out) {
  if (box->type->print)
    box->type->print(box, out);
  else {
    cx_dump(box, out);
  }
}

enum cx_cmp cx_cmp_box(const void *x, const void *y) {
  return cx_cmp(x, y);
}
