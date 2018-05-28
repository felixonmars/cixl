#include <string.h>

#include "cixl/call.h"
#include "cixl/cont.h"
#include "cixl/cx.h"
#include "cixl/error.h"
#include "cixl/scope.h"

struct cx_cont *cx_cont_init(struct cx_cont *c, struct cx *cx) {
  c->cx = cx;
  c->prev_coro = NULL;
  c->prev_task = NULL;
  c->prev_bin = c->bin = NULL;
  c->prev_pc = c->pc = -1;
  c->prev_nlibs = c->prev_nscopes = c->prev_ncalls = -1;
  cx_vec_init(&c->libs, sizeof(struct cx_lib *));
  cx_vec_init(&c->scopes, sizeof(struct cx_scope *));
  cx_vec_init(&c->calls, sizeof(struct cx_call));
  return c;
}

struct cx_cont *cx_cont_deinit(struct cx_cont *c) {
  cx_do_vec(&c->scopes, struct cx_scope *, s) { cx_scope_deref(*s); }
  cx_do_vec(&c->calls, struct cx_call, call) { cx_call_deinit(call); }
  
  cx_vec_deinit(&c->libs);
  cx_vec_deinit(&c->scopes);
  cx_vec_deinit(&c->calls);
  return c;
}

void cx_cont_reset(struct cx_cont *c) {
  struct cx *cx = c->cx;
  c->prev_coro = cx->coro;
  c->prev_task = cx->task;
  c->prev_bin = cx->bin;
  c->prev_pc = cx->pc;
  c->prev_nlibs = cx->libs.count;
  c->prev_nscopes = cx->scopes.count;
  c->prev_ncalls = cx->ncalls;
}

void cx_cont_return(struct cx_cont *c) {
  struct cx *cx = c->cx;
  cx->coro = c->prev_coro;
  cx->task = c->prev_task;
  c->bin = cx->bin;
  cx->bin = c->prev_bin;
  c->pc = cx->pc;
  cx->pc = c->prev_pc;

  ssize_t nlibs = cx->libs.count - c->prev_nlibs;
  
  if (nlibs) {
    cx_vec_grow(&c->libs, nlibs);

    memcpy(c->libs.items,
	   cx_vec_get(&cx->libs, c->prev_nlibs),
	   sizeof(struct cx_lib *) * nlibs);

    c->libs.count = nlibs;
    cx->libs.count -= nlibs;
    cx->lib -= nlibs;
  }

  ssize_t nscopes = cx->scopes.count - c->prev_nscopes;
  
  if (nscopes) {
    cx_vec_grow(&c->scopes, nscopes);

    memcpy(c->scopes.items,
	   cx_vec_get(&cx->scopes, c->prev_nscopes),
	   sizeof(struct cx_scope *) * nscopes);

    c->scopes.count = nscopes;
    cx->scopes.count -= nscopes;
    cx->scope -= nscopes;
  }

  ssize_t ncalls = cx->ncalls - c->prev_ncalls;
  
  if (ncalls) {
    cx_vec_grow(&c->calls, ncalls);
    memcpy(c->calls.items, cx->calls+c->prev_ncalls, sizeof(struct cx_call) * ncalls);
    c->calls.count = ncalls;
    cx->ncalls -= ncalls;
  }
}

void cx_cont_resume(struct cx_cont *c) {
  struct cx *cx = c->cx;
  cx_cont_reset(c);
  cx->bin = c->bin;
  cx->pc = c->pc;

  if (c->libs.count) {
    cx_vec_grow(&cx->libs, cx->libs.count+c->libs.count);
    
    memcpy(cx_vec_end(&cx->libs),
	   c->libs.items,
	   sizeof(struct cx_lib *) * c->libs.count);
    
    cx->libs.count += c->libs.count;
    cx->lib = cx_vec_peek(&cx->libs, 0);
    cx_vec_clear(&c->libs);
  }

  if (c->scopes.count) {
    cx_vec_grow(&cx->scopes, cx->scopes.count+c->scopes.count);
    
    memcpy(cx_vec_end(&cx->scopes),
	   c->scopes.items,
	   sizeof(struct cx_scope *) * c->scopes.count);
    
    cx->scopes.count += c->scopes.count;
    cx->scope = cx_vec_peek(&cx->scopes, 0);
    cx_vec_clear(&c->scopes);
  }

  if (c->calls.count) {
    memcpy(cx->calls+cx->ncalls,
	   c->calls.items,
	   sizeof(struct cx_call) * c->calls.count);
    
    cx->ncalls += c->calls.count;
    cx_vec_clear(&c->calls);
  }
}

void cx_cont_finish(struct cx_cont *c) {
  struct cx *cx = c->cx;
  cx->coro = c->prev_coro;
  cx->task = c->prev_task;
  cx->bin = c->prev_bin;
  cx->pc = c->prev_pc;

  while (cx->libs.count > c->prev_nlibs) { cx_pop_lib(cx); }
  while (cx->scopes.count > c->prev_nscopes) { cx_pop_scope(cx, false); }
  while (cx->ncalls > c->prev_ncalls) { cx_test(cx_pop_call(cx)); }
}
