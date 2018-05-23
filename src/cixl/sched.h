#ifndef CX_SCHED_H
#define CX_SCHED_H

#include <semaphore.h>
#include <stdbool.h>

#include "cixl/ls.h"

struct cx_box;
struct cx_lib;
struct cx_scope;
struct cx_task;
struct cx_type;

struct cx_sched {
  struct cx *cx;  
  struct cx_ls tasks;
  sem_t go;
  size_t ntasks, nrescheds, nrefs;
};

struct cx_sched *cx_sched_new(struct cx *cx);
struct cx_sched *cx_sched_ref(struct cx_sched *s);
void cx_sched_deref(struct cx_sched *s);
bool cx_sched_push(struct cx_sched *s, struct cx_box *action);
bool cx_sched_yield(struct cx_sched *s);
bool cx_sched_run(struct cx_sched *s, struct cx_scope *scope);

struct cx_type *cx_init_sched_type(struct cx_lib *lib);

#endif
