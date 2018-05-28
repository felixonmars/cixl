#ifndef CX_CORO_H
#define CX_CORO_H

#include <pthread.h>
#include <semaphore.h>
#include <stdbool.h>
#include "cixl/box.h"
#include "cixl/cont.h"

struct cx;
struct cx_lib;
struct cx_type;

#define CX_CORO_STACK_SIZE (32*1024)

enum cx_coro_state {CX_CORO_NEW, CX_CORO_RUN, CX_CORO_DONE, CX_CORO_FREE};

struct cx_coro {
  struct cx *cx;
  struct cx_cont cont;
  struct cx_box action;
  pthread_t thread;
  sem_t on_call, on_return;
  enum cx_coro_state state;
  struct cx_coro *prev_coro;
  unsigned int nrefs;
};


struct cx_coro *cx_coro_new(struct cx *cx,
			    struct cx_box *action);

struct cx_coro *cx_coro_ref(struct cx_coro *c);
void cx_coro_deref(struct cx_coro *c);
struct cx_coro *cx_coro_deinit(struct cx_coro *c);
bool cx_coro_call(struct cx_coro *c, struct cx_scope *scope);
bool cx_coro_reset(struct cx_coro *c);
bool cx_coro_cancel(struct cx_coro *c);
bool cx_coro_return(struct cx_coro *c, struct cx_scope *scope);

struct cx_type *cx_init_coro_type(struct cx_lib *lib);

#endif
