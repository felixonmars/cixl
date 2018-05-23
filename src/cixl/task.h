#ifndef CX_TASK_H
#define CX_TASK_H

#include <pthread.h>

#include "cixl/box.h"
#include "cixl/ls.h"

#define CX_TASK_STACK_SIZE 32000

struct cx_sched;
struct cx_scope;

enum cx_task_state {CX_TASK_NEW, CX_TASK_RUN};

struct cx_task {
  struct cx_sched *sched;
  struct cx_box action;
  enum cx_task_state state;
  pthread_t thread;
  struct cx_task *prev_task;
  ssize_t prev_pc, pc;
  struct cx_bin *prev_bin, *bin;
  ssize_t prev_nlibs, prev_nscopes, prev_ncalls;
  struct cx_vec libs, scopes, calls;
  struct cx_ls q;
};

struct cx_task *cx_task_init(struct cx_task *t,
			     struct cx_sched *sched,
			     struct cx_box *action);

struct cx_task *cx_task_deinit(struct cx_task *t);
bool cx_task_resched(struct cx_task *t, struct cx_scope *scope);
bool cx_task_start(struct cx_task *t);

#endif
