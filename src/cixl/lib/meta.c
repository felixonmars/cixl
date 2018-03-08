#include <string.h>

#include "cixl/arg.h"
#include "cixl/bin.h"
#include "cixl/cx.h"
#include "cixl/error.h"
#include "cixl/fimp.h"
#include "cixl/func.h"
#include "cixl/lib.h"
#include "cixl/lib/meta.h"
#include "cixl/op.h"
#include "cixl/scope.h"
#include "cixl/str.h"

static ssize_t lib_eval(struct cx_macro_eval *eval,
			    struct cx_bin *bin,
			    size_t tok_idx,
			    struct cx *cx) {
  struct cx_tok *t = cx_vec_start(&eval->toks);
  struct cx_lib *lib = t->as_lib;
  struct cx_op *op = cx_op_init(bin, CX_OLIBDEF(), tok_idx);
  op->as_libdef.lib = lib;
  op->as_libdef.init = lib->inits.count;
  size_t start_pc = bin->ops.count;
  cx_op_init(bin, CX_OPUSHLIB(), tok_idx)->as_pushlib.lib = lib;
  t++;
  
  if (!cx_compile(cx, t, cx_vec_end(&eval->toks), bin)) {
    cx_error(cx, cx->row, cx->col, "Failed compiling lib");
    goto exit;
  }

  cx_op_init(bin, CX_OPOPLIB(), tok_idx);
  cx_op_init(bin, CX_OSTOP(), tok_idx);
  cx_lib_push_init(lib, cx_lib_ops(bin, start_pc, bin->ops.count-start_pc));
  
 exit:
  return tok_idx+1;
}

static bool lib_parse(struct cx *cx, FILE *in, struct cx_vec *out) {
  int row = cx->row, col = cx->col;
  struct cx_macro_eval *eval = cx_macro_eval_new(lib_eval);

  if (!cx_parse_tok(cx, in, &eval->toks, false)) {
    cx_error(cx, row, col, "Missing lib id");
    cx_macro_eval_deref(eval);
    return false;
  }

  struct cx_tok *id = cx_vec_peek(&eval->toks, 0);

  if (id->type != CX_TID()) {
    cx_error(cx, id->row, id->col, "Invalid lib id: %s", id->type->id);
    cx_macro_eval_deref(eval);
    return false;
  }

  struct cx_lib *lib = cx_add_lib(cx, id->as_ptr);
  cx_tok_deinit(id);
  cx_tok_init(id, CX_TLIB(), id->row, id->col)->as_lib = lib;
  struct cx_lib *prev = *cx->lib;
  cx_push_lib(cx, lib);
  cx_lib_use(prev);
  
  if (!cx_parse_end(cx, in, &eval->toks, true)) {
    if (!cx->errors.count) { cx_error(cx, row, col, "Missing lib: end"); }
    cx_pop_lib(cx);
    cx_macro_eval_deref(eval);
    return false;
  }

  cx_pop_lib(cx);
  cx_tok_init(cx_vec_push(out), CX_TMACRO(), row, col)->as_ptr = eval;
  return true;
}

static ssize_t use_eval(struct cx_macro_eval *eval,
			struct cx_bin *bin,
			size_t tok_idx,
			struct cx *cx) {
  cx_op_init(bin, CX_OUSE(), tok_idx);
  return tok_idx+1;
}

static bool use_parse(struct cx *cx, FILE *in, struct cx_vec *out) {
  int row = cx->row, col = cx->col;
  struct cx_macro_eval *eval = cx_macro_eval_new(use_eval);
  
  if (!cx_parse_end(cx, in, &eval->toks, false)) {
    if (!cx->errors.count) { cx_error(cx, row, col, "Missing use: end"); }
    cx_macro_eval_deref(eval);
    return false;
  }

  cx_do_vec(&eval->toks, struct cx_tok, t) {
    if (t->type != CX_TID() && t->type != CX_TGROUP()) {
      cx_error(cx, t->row, t->col, "Invalid lib: %s", t->type->id);
      cx_macro_eval_deref(eval);
      return false;
    }
    
    if (t->type == CX_TID()) {
      if (!cx_use(cx, t->as_ptr)) {
	cx_macro_eval_deref(eval);
	return false;
      }

    } else {
      struct cx_tok *tt = cx_vec_get(&t->as_vec, 0);

      if (tt->type != CX_TID()) {
	cx_error(cx, tt->row, tt->col, "Invalid id: %s", tt->type->id);
	cx_macro_eval_deref(eval);
	return false;
      }

      const char *lib = tt->as_ptr;
      tt++;

      struct cx_vec ids;
      cx_vec_init(&ids, sizeof(const char *));

      for (; tt != cx_vec_end(&t->as_vec); tt++) {
	if (tt->type != CX_TID()) {
	  cx_error(cx, tt->row, tt->col, "Invalid id: %s", tt->type->id);
	  cx_vec_deinit(&ids);
	  cx_macro_eval_deref(eval);
	  return false;
	}
	
	*(char **)cx_vec_push(&ids) = tt->as_ptr;
      }

      if (!cx_vuse(cx, lib, ids.count, (const char **)ids.items)) {
	cx_vec_deinit(&ids);
	cx_macro_eval_deref(eval);
	return false;
      }

      cx_vec_deinit(&ids);
    }        
  }
  
  cx_tok_init(cx_vec_push(out), CX_TMACRO(), row, col)->as_ptr = eval;
  return true;
}

static bool define_parse(struct cx *cx, FILE *in, struct cx_vec *out) {
  int row = cx->row, col = cx->col;
  struct cx_vec toks;
  cx_vec_init(&toks, sizeof(struct cx_tok));
  bool ok = false;
  
  if (!cx_parse_tok(cx, in, &toks, false)) {
    cx_error(cx, row, col, "Missing define id");
    goto exit1;
  }

  struct cx_tok id_tok = *(struct cx_tok *)cx_vec_pop(&toks);

  if (id_tok.type != CX_TID() && id_tok.type != CX_TGROUP()) {
    cx_error(cx, id_tok.row, id_tok.col, "Invalid define id");
    goto exit1;
  }

  if (!cx_parse_end(cx, in, &toks, true)) {
    if (!cx->errors.count) { cx_error(cx, row, col, "Missing define end"); }
    goto exit1;
  }

  struct cx_scope *s = cx_begin(cx, NULL);
  if (!cx_eval_toks(cx, &toks)) { goto exit2; }

  bool put(const char *id, struct cx_type *type) {
    struct cx_box *src = cx_pop(s, true);

    if (!src) {
      cx_error(cx, row, col, "Missing value for id: %s", id);
      return false;
    }
    
    struct cx_box *dst = cx_put_const(*cx->lib, cx_sym(cx, id), false);
    if (!dst) { return false; }
    *dst = *src;
    return true;
  }

  if (id_tok.type == CX_TID()) {
    if (!put(id_tok.as_ptr, NULL)) { goto exit2; }
  } else {
    struct cx_vec *id_toks = &id_tok.as_vec, ids, types;
    cx_vec_init(&ids, sizeof(struct cx_tok));
    cx_vec_init(&types, sizeof(struct cx_type *));
    
    bool push_type(struct cx_type *type) {
      if (ids.count == types.count) {
	cx_error(cx, cx->row, cx->col, "Missing define id");
	return false;
      }
      
      for (struct cx_tok *id = cx_vec_get(&ids, types.count);
	   id != cx_vec_end(&ids);
	   id++) {
	*(struct cx_type **)cx_vec_push(&types) = type;	
      }

      return true;
    }
    
    cx_do_vec(id_toks, struct cx_tok, t) {
      if (t->type == CX_TID()) {
	*(struct cx_tok *)cx_vec_push(&ids) = *t;
      } else if (t->type == CX_TTYPE() && !push_type(t->as_ptr)) {
	goto exit3;
      }
    }

    if (ids.count > types.count && !push_type(NULL)) { goto exit3; }
    struct cx_tok *id = cx_vec_peek(&ids, 0);
    struct cx_type **type = cx_vec_peek(&types, 0);
    
    for (; id >= (struct cx_tok *)ids.items; id--, type--) {
      if (!put(id->as_ptr, *type)) { goto exit3; }
    }    

    ok = true;
  exit3:
    cx_vec_deinit(&ids);
    cx_vec_deinit(&types);
  }
  
 exit2:
  cx_end(cx);
 exit1: {
    cx_do_vec(&toks, struct cx_tok, t) { cx_tok_deinit(t); }
    cx_vec_deinit(&toks);
    return ok;
  }
}

static bool cx_lib_imp(struct cx_scope *scope) {
  cx_box_init(cx_push(scope), scope->cx->lib_type)->as_lib = *scope->cx->lib;
  return true;
}

static bool lib_id_imp(struct cx_scope *scope) {
  struct cx_box lib = *cx_test(cx_pop(scope, false));
  cx_box_init(cx_push(scope), scope->cx->sym_type)->as_sym = lib.as_lib->id;
  return true;
}

static bool get_lib_imp(struct cx_scope *scope) {
  struct cx *cx = scope->cx;
  struct cx_box id = *cx_test(cx_pop(scope, false));
  struct cx_lib *lib = cx_get_lib(cx, id.as_sym.id, true);

  if (lib) {
    cx_box_init(cx_push(scope), cx->lib_type)->as_lib = lib;
  } else {
    cx_box_init(cx_push(scope), cx->nil_type);
  }
  
  return true;
}

cx_lib(cx_init_meta, "cx/meta") {
  struct cx *cx = lib->cx;
    
  if (!cx_use(cx, "cx/abc", "Lib", "Opt", "Str", "Sym")) {
    return false;
  }
    
  cx_add_macro(lib, "lib:", lib_parse);
  cx_add_macro(lib, "use:", use_parse);
  cx_add_macro(lib, "define:", define_parse);

  cx_add_cfunc(lib, "cx-lib",
	       cx_args(),
	       cx_args(cx_arg(NULL, cx->lib_type)),
	       cx_lib_imp);

  cx_add_cfunc(lib, "id",
	       cx_args(cx_arg("lib", cx->lib_type)),
	       cx_args(cx_arg(NULL, cx->sym_type)),
	       lib_id_imp);

  cx_add_cfunc(lib, "get-lib",
	       cx_args(cx_arg("id", cx->sym_type)),
	       cx_args(cx_arg(NULL, cx->opt_type)),
	       get_lib_imp);

  return true;
}
