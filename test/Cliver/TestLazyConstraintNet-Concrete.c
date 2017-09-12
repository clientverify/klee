// RUN: %llvmgcc -B/usr/lib/x86_64-linux-gnu %s "%srcroot/lib/Basic/KTest.cpp" -DKTEST=\"%t1.ktest\" -o %t1
// RUN: %llvmgcc %s -DKLEE -DCLIENT -emit-llvm -g -c -o %t1.bc

// First set: fully concrete execution (should follow expected verifier result)

// Positive test case: client sends 6410, 10
// RUN: %t1 > %t1.log
// RUN: grep -q "CLIENT: success" %t1.log
// RUN: grep -q "SERVER: success" %t1.log
// RUN: not grep -q "error" %t1.log
// RUN: rm -rf %t2.dir
// RUN: %klee --output-dir=%t2.dir -cliver -optimize=0 -posix-runtime -libc=uclibc -socket-log=%t1.ktest %t1.bc &> %t2.log
// RUN: grep -q "Verifier Result: success" %t2.log

// Negative test case: client sends 6410, 11
// RUN: %t1 -n > %t3.log
// RUN: grep -q "CLIENT: success" %t3.log
// RUN: grep -q "SERVER: success" %t3.log
// RUN: not grep -q "error" %t3.log
// RUN: rm -rf %t4.dir
// RUN: not %klee --output-dir=%t4.dir -cliver -optimize=0 -posix-runtime -libc=uclibc -socket-log=%t1.ktest %t1.bc &> %t4.log
// RUN: not grep -q "Verifier Result: success" %t4.log

#include "TestLazyConstraint.inc.c"
