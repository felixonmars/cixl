#ifndef CX_CALL_H
#define CX_CALL_H

#include "cixl/vec.h"

struct cx_call {
  int row, col;
  struct cx_fimp *fimp;
  struct cx_scope *scope;
  struct cx_vec args;
  int recalls;
};

struct cx_call *cx_call_init(struct cx_call *c,
			     int row, int col,
			     struct cx_fimp *fimp,
			     struct cx_scope *scope);

struct cx_call *cx_call_deinit(struct cx_call *c);
struct cx_box *cx_call_arg(struct cx_call *c, int i);
bool cx_call_pop_args(struct cx_call *c);

#endif
