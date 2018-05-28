#ifndef CX_CONT_H
#define CX_CONT_H

#include "cixl/vec.h"

struct cx;
struct cx_task;

struct cx_cont {
  struct cx *cx;
  struct cx_coro *prev_coro;
  struct cx_task *prev_task;
  ssize_t prev_pc, pc;
  struct cx_bin *prev_bin, *bin;
  ssize_t prev_nlibs, prev_nscopes, prev_ncalls;
  struct cx_vec libs, scopes, calls;
};

struct cx_cont *cx_cont_init(struct cx_cont *c, struct cx *cx);
struct cx_cont *cx_cont_deinit(struct cx_cont *c);
void cx_cont_reset(struct cx_cont *c);
void cx_cont_return(struct cx_cont *c);
void cx_cont_resume(struct cx_cont *c);
void cx_cont_finish(struct cx_cont *c);

#endif
