// RUN: %llvmgcc -B/usr/lib/x86_64-linux-gnu %s "%srcroot/lib/Basic/KTest.cpp" -DKTEST=\"%t1.ktest\" -o %t1
// RUN: %llvmgcc %s -DKLEE -DCLIENT -emit-llvm -g -c -o %t1.bc 
// RUN: %t1 > %t1.log
// RUN: grep -q "CLIENT: success" %t1.log
// RUN: grep -q "SERVER: success" %t1.log
// RUN: not grep -q "error" %t1.log
// RUN: rm -rf %t1.cliver-out
// RUN: %klee -use-threads=4 -use-forked-solver=0 -output-dir=%t1.cliver-out -cliver -optimize=0 -posix-runtime -libc=uclibc -socket-log=%t1.ktest %t1.bc &> %t1.cliver.log
// RUN: grep -q "CLIENT: success" %t1.cliver.log
// UN: rm -rf %t2.cliver-out
// UN: %klee -use-threads=4 -use-forked-solver=0 -cliver -cliver-mode=training -optimize=0 -posix-runtime -libc=uclibc -output-dir=%t2.cliver-out -socket-log=%t1.ktest %t1.bc &> %t2.cliver.log
// UN: grep -q "CLIENT: success" %t2.cliver.log
// UN: rm -rf %t3.cliver-out
// UN: %klee -use-threads=4 -use-forked-solver=0 -cliver -use-clustering -cliver-mode=edit-dist-kprefix-row -optimize=0 -posix-runtime -libc=uclibc -output-dir=%t3.cliver-out -training-path-dir=%t2.cliver-out -socket-log=%t1.ktest %t1.bc &> %t3.cliver.log
// UN: not grep -q "Recomputed kprefix" %t3.cliver.log
// UN: grep -q "CLIENT: success" %t3.cliver.log
// REQUIRES: threads

#include "ClientServer.c"

