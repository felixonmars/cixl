:- module bench7.

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
:- import_module thread.mvar.
:- import_module thread.semaphore.
:- import_module string.

main(!IO) :-
    N = 1000,
    benchmark_det_io(test, unit, _, !IO, N, Time),
    format("%.3fms\n", [f((float(Time) / float(N)))], !IO).

%
% This benchmark creates an mvar and two threads.  The mvar is used to pass
% a token between the two threads 10,000 times.  The main thread waits for
% both threads to finish before exiting.
%

:- pred test(unit::in, unit::out, io::di, io::uo) is det.

test(!Unit, !IO) :-
    promise_equivalent_solutions [!:IO] (
        N = 10000,
        semaphore.init(Sem, !IO),
        mvar.init(MVar1, !IO),
        mvar.init(MVar2, !IO),
        put(MVar1, unit, !IO),
        spawn(task(1, N, Sem, MVar1, MVar2), !IO),
        spawn(task(2, N, Sem, MVar2, MVar1), !IO),
        wait(Sem, !IO),
        wait(Sem, !IO)
    ).


:- pred task(int::in, int::in, semaphore::in, mvar(unit)::in, mvar(unit)::in,
    io::di, io::uo) is cc_multi.

task(Id, N, Sem, MVarGet, MVarPut, !IO) :-
    ( if N > 0 then
        mvar.take(MVarGet, Token, !IO),
        mvar.put(MVarPut, Token, !IO),
        task(Id, N - 1, Sem, MVarGet, MVarPut, !IO)
    else
        signal(Sem, !IO)
    ).

