// RUN: %llvmgcc -B/usr/lib/x86_64-linux-gnu %s "../../lib/Basic/KTest.cpp" -DKTEST=\"%t1.ktest\" -o %t1
// RUN: %llvmgcc %s -DKLEE -DCLIENT -emit-llvm -g -c -o %t1.bc 
// RUN: %t1 -e 1 > %t1.log
// RUN: grep -q "CLIENT: success" %t1.log
// RUN: grep -q "SERVER: success" %t1.log
// RUN: not grep -q "error" %t1.log
// RUN: rm -rf %t1.cliver-out
// RUN: %klee --output-dir=%t1.cliver-out -cliver -search-mode=pq -use-full-variable-names=1 -optimize=0 -posix-runtime -libc=uclibc -socket-log=%t1.ktest %t1.bc -e 1 &> %t1.cliver.log
// RUN: grep -q "CLIENT: success" %t1.cliver.log
// RUN: rm -rf %t2.cliver-out
// RUN: %klee -cliver -cliver-mode=training -optimize=0 -posix-runtime -libc=uclibc -output-dir=%t2.cliver-out -socket-log=%t1.ktest %t1.bc -e 1 &> %t2.cliver.log
// RUN: grep -q "CLIENT: success" %t2.cliver.log
// RUN: grep -q "Writing round_0001_" %t2.cliver.log
// RUN: grep -q "Writing round_0002_" %t2.cliver.log
// RUN: grep -q "Writing round_0003_" %t2.cliver.log
// RUN: grep -q "Writing round_0004_" %t2.cliver.log
// RUN: grep -q "Writing round_0005_" %t2.cliver.log
// RUN: grep -q "Writing round_0006_" %t2.cliver.log
// RUN: rm -rf %t3.cliver-out
// RUN: %klee -cliver -use-clustering -cliver-mode=edit-dist-kprefix-row -optimize=0 -posix-runtime -libc=uclibc -output-dir=%t3.cliver-out -training-path-dir=%t2.cliver-out -socket-log=%t1.ktest %t1.bc -e 1 &> %t3.cliver.log
// RUN: not grep -q "Recomputed kprefix" %t3.cliver.log
// RUN: grep -q "CLIENT: success" %t3.cliver.log

#include "ClientServer.c"
