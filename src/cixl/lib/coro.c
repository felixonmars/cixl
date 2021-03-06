#include <errno.h>
#include <string.h>
#include <unistd.h>

#include "cixl/arg.h"
#include "cixl/call.h"
#include "cixl/cx.h"
#include "cixl/error.h"
#include "cixl/fimp.h"
#include "cixl/func.h"
#include "cixl/lib.h"
#include "cixl/lib/coro.h"
#include "cixl/sched.h"
#include "cixl/scope.h"
#include "cixl/coro.h"

static bool coro_imp(struct cx_call *call) {
  struct cx_scope *s = call->scope;
  struct cx_box *a = cx_test(cx_call_arg(call, 0));
  cx_box_init(cx_push(s), s->cx->coro_type)->as_ptr = cx_coro_new(s->cx, a);
  return true;
}

static bool suspend_imp(struct cx_call *call) {
  struct cx_scope *s = call->scope;
  struct cx_coro *c = s->cx->coro;

  if (!c) {
    cx_error(s->cx, s->cx->row, s->cx->col, "Nothing to suspend");
    return false;
  }
  
  return cx_coro_suspend(c, s);
}

static bool reset_imp(struct cx_call *call) {
  struct cx_coro *c = cx_test(cx_call_arg(call, 0))->as_ptr;
  return cx_coro_reset(c);
}

static bool cancel_imp(struct cx_call *call) {
  struct cx_coro *c = cx_test(cx_call_arg(call, 0))->as_ptr;
  return cx_coro_cancel(c);
}

cx_lib(cx_init_coro, "cx/coro") {    
  struct cx *cx = lib->cx;
    
  if (!cx_use(cx, "cx/abc", "A", "Seq")) {
    return false;
  }

  cx->coro_type = cx_init_coro_type(lib);
  
  cx_add_cfunc(lib, "coro",
	       cx_args(cx_arg("action", cx->any_type)),
	       cx_args(cx_arg(NULL, cx->coro_type)),
	       coro_imp);

  cx_add_cfunc(lib, "suspend",
	       cx_args(),
	       cx_args(),
	       suspend_imp);

  cx_add_cfunc(lib, "reset",
	       cx_args(cx_arg("coro", cx->coro_type)),
	       cx_args(),
	       reset_imp);

  cx_add_cfunc(lib, "cancel",
	       cx_args(cx_arg("coro", cx->coro_type)),
	       cx_args(),
	       cancel_imp);

  return true;
}
