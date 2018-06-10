
:- module bench5.

:- interface.

:- import_module io.

:- pred main(io::di, io::uo) is cc_multi.

:- implementation.

:- import_module benchmarking.
:- import_module int.
:- import_module float.
:- import_module list.
:- import_module unit.
:- import_module thread.
:- import_module thread.semaphore.
:- import_module string.

main(!IO) :-
    N = 1000,
    benchmark_det_io(test, unit, _, !IO, N, Time),
    format("%.3fms\n", [f((float(Time) / float(N)))], !IO).

% Create two threads that yield 10,000 times each.  There is no
% synchronisation between any of the threads.  Test this using
% MERCURY_OPTIONS=-P1 to ensure both threads take in turns using the CPU and
% you actually test Mercury's context switching.
:- pred test(unit::in, unit::out, io::di, io::uo) is det.

test(!Unit, !IO) :-
    promise_equivalent_solutions [!:IO] (
        N = 10000,
        semaphore.init(Sem, !IO),
        spawn(task(N, Sem), !IO),
        spawn(task(N, Sem), !IO),
        wait(Sem, !IO),
        wait(Sem, !IO)
    ).

:- pred task(int::in, semaphore::in, io::di, io::uo) is cc_multi.

task(N, Sem, !IO) :-
    ( if N > 0 then
        yield(!IO),
        task(N - 1, Sem, !IO)
    else
        signal(Sem, !IO)
    ).

