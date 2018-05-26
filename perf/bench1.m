
:- module bench1.

:- interface.

:- import_module io.

:- pred main(io::di, io::uo) is cc_multi.

:- implementation.

:- import_module benchmarking.
:- import_module bool.
:- import_module int.
:- import_module integer.
:- import_module float.
:- import_module list.
:- import_module string.

main(!IO) :-
    N = 1000000,
    benchmark_func(fib, 50, _, N, TimeInt),
    format("int: %.3fms\n", [f((float(TimeInt) / float(N)))], !IO),
    benchmark_func(fib, 50.0, _, N, TimeFloat),
    format("float: %.3fms\n", [f((float(TimeFloat) / float(N)))], !IO),
    benchmark_func(fib, integer(50), _, N, TimeInteger),
    format("integer: %.3fms\n", [f((float(TimeInteger) / float(N)))], !IO).

:- func fib(I) = I <= fib_ready(I).

fib(N) = fib(zero, one, N).

:- func fib(I, I, I) = I <= fib_ready(I).

fib(A, B, N) =
    ( if gt_zero(N) then
        fib(B, add(A, B), dec(N))
    else
        A
    ).

% I'm using a typeclass so that I can run the fib code with any number type.

:- typeclass fib_ready(I) where [
    func zero = I,
    func one = I,
    pred gt_zero(I::in) is semidet,
    func add(I, I) = I,
    func dec(I) = I
].

:- instance fib_ready(int) where [
    zero = 0,
    one = 1,
    gt_zero(N)  :-
        N > 0,
    add(A, B) = A + B,
    dec(N) = N - 1
].

:- instance fib_ready(float) where [
    zero = 0.0,
    one = 1.0,
    gt_zero(N)  :-
        N > 0.0,
    add(A, B) = A + B,
    dec(N) = N - 1.0
].

:- instance fib_ready(integer) where [
    func(zero/0) is integer.zero,
    func(one/0) is integer.one,
    gt_zero(N)  :-
        N > integer.zero,
    add(A, B) = A + B,
    dec(N) = N - integer.one
].
