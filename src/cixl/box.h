#ifndef CX_BOX_H
#define CX_BOX_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "cixl/color.h"
#include "cixl/float.h"
#include "cixl/point.h"
#include "cixl/sym.h"
#include "cixl/time.h"

struct cx_error;
struct cx_file;
struct cx_func;
struct cx_iter;
struct cx_lambda;
struct cx_lib;
struct cx_pair;
struct cx_poll;
struct cx_proc;
struct cx_queue;
struct cx_ref;
struct cx_sched;
struct cx_scope;
struct cx_str;
struct cx_table;
struct cx_type;

struct cx_box {
  struct cx_type *type;
  
  union {
    bool             as_bool;
    unsigned char    as_char;
    struct cx_color  as_color;
    struct cx_error *as_error;
    struct cx_file  *as_file;
    cx_float_t       as_float;
    int64_t          as_int;
    struct cx_iter  *as_iter;
    struct cx_lib   *as_lib;
    struct cx_pair  *as_pair;
    struct cx_point  as_point;
    struct cx_proc  *as_proc;
    struct cx_poll  *as_poll;
    void            *as_ptr;
    struct cx_queue *as_queue;
    struct cx_ref   *as_ref;
    struct cx_sched *as_sched;
    struct cx_str   *as_str;
    struct cx_sym    as_sym;
    struct cx_table *as_table;
    struct cx_time   as_time;
  };
};

struct cx_box *cx_box_new(struct cx_type *type);
struct cx_box *cx_box_init(struct cx_box *box, struct cx_type *type);
struct cx_box *cx_box_deinit(struct cx_box *box);
bool cx_box_emit(struct cx_box *box, const char *exp, FILE *out);

bool cx_eqval(struct cx_box *x, struct cx_box *y);
bool cx_equid(struct cx_box *x, struct cx_box *y);
enum cx_cmp cx_cmp(const struct cx_box *x, const struct cx_box *y);
bool cx_ok(struct cx_box *x);
bool cx_call(struct cx_box *box, struct cx_scope *scope);
struct cx_box *cx_copy(struct cx_box *dst, const struct cx_box *src);
struct cx_box *cx_clone(struct cx_box *dst, struct cx_box *src);
void cx_iter(struct cx_box *in, struct cx_box *out);
bool cx_sink(struct cx_box *dst, struct cx_box *v);
bool cx_write(struct cx_box *box, FILE *out);
void cx_dump(struct cx_box *box, FILE *out);
void cx_print(struct cx_box *box, FILE *out);

enum cx_cmp cx_cmp_box(const void *x, const void *y);

#endif
