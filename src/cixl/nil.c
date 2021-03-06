#include "cixl/cx.h"
#include "cixl/box.h"
#include "cixl/error.h"
#include "cixl/nil.h"
#include "cixl/scope.h"

static bool equid_imp(struct cx_box *x, struct cx_box *y) {
  return true;
}

static bool ok_imp(struct cx_box *x) {
  return false;
}

static void dump_imp(struct cx_box *v, FILE *out) {
  fputs("#nil", out);
}

static bool emit_imp(struct cx_box *v, const char *exp, FILE *out) {
  fprintf(out, "cx_box_init(%s, cx->nil_type);", exp);
  return true;
}

struct cx_type *cx_init_nil_type(struct cx_lib *lib) {
  struct cx *cx = lib->cx;
  struct cx_type *t = cx_add_type(lib, "Nil", cx->opt_type);
  t->equid = equid_imp;
  t->ok = ok_imp;
  t->write = dump_imp;
  t->dump = dump_imp;
  t->emit = emit_imp;
  return t;
}
