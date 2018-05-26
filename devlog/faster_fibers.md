## Faster Fibers using Threads
#### 2018-05-24

### Intro
In a previous [post](https://github.com/basic-gongfu/cixl/blob/master/devlog/minimal_fibers.md), I presented [Cixl](https://github.com/basic-gongfu/cixl)'s implementation of fibers on top of [ucontext](http://pubs.opengroup.org/onlinepubs/7908799/xsh/ucontext.h.html). One of the issues with ucontext is that it does more than needed for a fiber scheduler, another is that the functionality was deprecated a while back. In is place, the POSIX gods suggest using [pthreads](https://computing.llnl.gov/tutorials/pthreads/) instead. Until recently, I was under the impression that the reason we're going through all this trouble with user space scheduling is that it can't be implemented on top of preemptive threads without loosing most its qualities. But then I realized that I never really heard of anyone trying; and I mean really trying, not using it as a low performance fallback. A quick web search turned up [npth](https://github.com/gpg/npth) but that's about it; so I set out on a 3 day expedition into pthread land, mostly to prove to myself that it isn't possible. Except; from where I'm standing right now, it certainly looks like it might be. And besides being possible, and not deprecated; it's also potentially 2-3 times faster while supporting all but the most extreme fiber cases.

### Implementation
An initial implementation that switched back and forth between scheduler loop and fibers ran roughly 10 times slower than ucontext. But just as I was about to declare victory and move on, I realized that I would probably be better off reusing the native scheduler loop than replacing it. Making fibers responsible for forwarding the run signal without using a separate loop brought the time down to 2-3 times faster than ucontext. The scheduler potentially runs two threads at a time, the loop and the current fiber; which means that access to common resources has to be protected using locks or atomics.

The scheduler loop is reduced to launching the first and cleaning up finished fibers, it doesn't return until all fibers have been joined.

sched.[h](https://github.com/basic-gongfu/cixl/blob/master/src/cixl/sched.h)/[c](https://github.com/basic-gongfu/cixl/blob/master/src/cixl/sched.c)
```
bool cx_sched_run(struct cx_sched *s, struct cx_scope *scope) {
  if (sem_post(&s->go) != 0) {
    cx_error(s->cx, s->cx->row, s->cx->col, "Failed posting: %d", errno);
    return false;
  }
  
  while (atomic_load(&s->ntasks)) {
    if (sem_wait(&s->done) != 0) {
      cx_error(s->cx, s->cx->row, s->cx->col, "Failed waiting: %d", errno);
      return false;
    }
    
    if (pthread_mutex_lock(&s->q_lock) != 0) {
      cx_error(s->cx, s->cx->row, s->cx->col, "Failed locking: %d", errno);
      return false;
    }

    struct cx_task *t = cx_baseof(s->done_q.next, struct cx_task, q);
    cx_ls_delete(&t->q);
    free(cx_task_deinit(t));

    if (pthread_mutex_unlock(&s->q_lock) != 0) {
      cx_error(s->cx, s->cx->row, s->cx->col, "Failed unlocking: %d", errno);
      return false;
    }
  }

  while (s->done_q.next != &s->done_q) {
    struct cx_task *t = cx_baseof(s->done_q.next, struct cx_task, q);
    cx_ls_delete(&t->q);
    free(cx_task_deinit(t));
  }

  return true;
}
```

To enforce specified order, the scheduler waits for new fibers to start.

```
bool cx_sched_push(struct cx_sched *s, struct cx_box *action) {
  unsigned prev_ntasks = atomic_load(&s->ntasks);
  struct cx_task *t = cx_task_init(malloc(sizeof(struct cx_task)), s, action);
  cx_ls_prepend(&s->new_q, &t->q);
  bool ok = cx_task_start(t);
  
  if (!ok) {
    cx_ls_delete(&t->q);
    free(cx_task_deinit(t));
    return false;
  }

  while (atomic_load(&s->ntasks) == prev_ntasks) { sched_yield(); }
  return true;
}
```

While fibers update the scheduler and hit the ```go``` semaphore first thing.

task.[h](https://github.com/basic-gongfu/cixl/blob/master/src/cixl/task.h)/[c](https://github.com/basic-gongfu/cixl/blob/master/src/cixl/task.c)
```
bool run_next(struct cx_task *t) {
  struct cx *cx = t->sched->cx;
  size_t prev_nruns = t->sched->nruns;
  
  if (sem_post(&t->sched->go) != 0) {
    cx_error(cx, cx->row, cx->col, "Failed posting: %d", errno);
    return false;
  }
  
  while (atomic_load(&t->sched->ntasks) > 1 &&
	 atomic_load(&t->sched->nruns) == prev_nruns) {
    sched_yield();
  }
  
  if (sem_wait(&t->sched->go) != 0) {
    cx_error(cx, cx->row, cx->col, "Failed waiting: %d", errno);
    return false;
  }

  return true;
}

void *on_start(void *data) {
  struct cx_task *t = data;
  struct cx *cx = t->sched->cx;  
  atomic_fetch_add(&t->sched->ntasks, 1);
  
  if (sem_wait(&t->sched->go) != 0) {
    cx_error(cx, cx->row, cx->col, "Failed waiting: %d", errno);
    return NULL;
  }

  bool ok = false;
  
  while (!ok) {
    if (t->sched->new_q.next == &t->q) {
      cx_ls_delete(&t->q);
      cx_ls_prepend(&t->sched->ready_q, &t->q);
      ok = true;
    }

    if (!ok && !run_next(t)) { return NULL; }
  }
  
  struct cx_scope *scope = cx_scope(cx, 0);
  before_run(t, scope->cx);
  atomic_fetch_add(&t->sched->nruns, 1);
  cx_call(&t->action, scope);

  cx->task = t->prev_task;
  cx->bin = t->prev_bin;
  cx->pc = t->prev_pc;
  while (cx->libs.count > t->prev_nlibs) { cx_pop_lib(cx); }
  while (cx->scopes.count > t->prev_nscopes) { cx_pop_scope(cx, false); }
  while (cx->ncalls > t->prev_ncalls) { cx_test(cx_pop_call(cx)); }

  if (pthread_mutex_lock(&t->sched->q_lock) != 0) {
    cx_error(cx, cx->row, cx->col, "Failed locking: %d", errno);
    return false;
  }

  cx_ls_delete(&t->q);
  cx_ls_prepend(&t->sched->done_q, &t->q);

  if (pthread_mutex_unlock(&t->sched->q_lock) != 0) {
    cx_error(cx, cx->row, cx->col, "Failed unlocking: %d", errno);
    return false;
  }

  if (atomic_fetch_sub(&t->sched->ntasks, 1) > 1 &&
      sem_post(&t->sched->go) != 0) {
    cx_error(cx, cx->row, cx->col, "Failed posting go: %d", errno);
  }

  if (sem_post(&t->sched->done) != 0) {
    cx_error(cx, cx->row, cx->col, "Failed posting done: %d", errno);
  }

  return NULL;
}

bool cx_task_start(struct cx_task *t) {  
  pthread_attr_t a;
  pthread_attr_init(&a);
  pthread_attr_setstacksize(&a, CX_TASK_STACK_SIZE);
  bool ok = pthread_create(&t->thread, &a, on_start, t) == 0;

  if (!ok) {
    struct cx *cx = t->sched->cx;
    cx_error(cx, cx->row, cx->col, "Failed creating thread: %d", errno);
  }

  pthread_attr_destroy(&a);
  return ok;
}
```

### Benchmark
We'll start with the [benchmark](https://github.com/basic-gongfu/cixl/blob/master/devlog/minimal_fibers.md#performance) from the previous post. Measured time is displayed in milliseconds.

[bench5.rb](https://github.com/basic-gongfu/cixl/blob/master/perf/bench5.rb)
```
n = 1000000

task1 = Fiber.new {n.times {Fiber.yield}}
task2 = Fiber.new {n.times {Fiber.yield}}

t1 = Time.now
n.times { task1.resume task2.resume }
t2 = Time.now
delta = (t2 - t1) * 1000
puts "#{delta.to_i}"

$ ruby bench5.rb
3546
```

[bench5.cx](https://github.com/basic-gongfu/cixl/blob/master/perf/bench5.cx)
```
define: n 1000000;
let: s Sched new;

2 {$s {#n &resched times} push} times
{$s run} clock 1000000 / say

$ cixl bench5.cx
1257
```

One of the reasons often given for avoiding threads is that they are expensive to create. Ensuring that fibers start in specified order adds another performance penalty on top. Still, unless you're creating thousands of them in tight loops; I doubt this will be a problem in practice.

[bench6.rb](https://github.com/basic-gongfu/cixl/blob/master/perf/bench6.rb)
```
n = 1000
fs = []

n.times {fs << Fiber.new {Fiber.yield}}

t1 = Time.now
2.times {fs.each {|f| f.resume}}
t2 = Time.now
delta = (t2 - t1) * 1000
puts "#{delta.to_i}"

$ ruby bench6.rb
15
```

[bench6.cx](https://github.com/basic-gongfu/cixl/blob/master/perf/bench6.cx)
```
use: cx;

define: n 1000;
let: s Sched new;

#n {$s &resched push} times
{$s run} clock 1000000 / say

$ cixl bench6.cx
52
```

Give me a yell if something is unclear, wrong or missing. And please consider helping out with a donation via [paypal](https://paypal.me/basicgongfu) or [liberapay](https://liberapay.com/basic-gongfu/donate) if you find this worthwhile, every contribution counts.