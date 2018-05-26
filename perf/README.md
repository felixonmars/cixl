
= Benchmarks

== Mercury benchmarks.

Run the Mercury benchmarks in the asm_fast.gc.par.stseg grade on Linux.
This backend uses Mercury's green threads implementation.  The hlc.gc.par
grade (and java and csharp) backends will also work, but they use the host
environment's threading.  Eg: in hlc.gc.par pthreads.

