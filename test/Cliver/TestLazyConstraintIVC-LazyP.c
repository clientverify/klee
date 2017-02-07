// RUN: %llvmgcc -B/usr/lib/x86_64-linux-gnu %s "%srcroot/lib/Basic/KTest.cpp" -DKTEST=\"%t1.ktest\" -o %t1
// RUN: %llvmgcc %s -DKLEE -DCLIENT -emit-llvm -g -c -o %t1.bc

// Third set: prohibitive functions and lazy constraint generators for the
// forward p() function. (should follow expected verifier result)

// Positive test case: client sends 314, 159
// RUN: %t1 -i > %t1.log
// RUN: grep -q "CLIENT: success" %t1.log
// RUN: grep -q "SERVER: success" %t1.log
// RUN: not grep -q "error" %t1.log
// RUN: rm -rf %t2.dir
// RUN: %klee --output-dir=%t2.dir -cliver -enable-lazy-constraints -optimize=0 -posix-runtime -libc=uclibc -socket-log=%t1.ktest %t1.bc -i -P -Lp &> %t2.log
// RUN: grep -q "Verifier Result: success" %t2.log

// Positive test case: client sends 314, 6410
// RUN: %t1 -i -t > %t3.log
// RUN: grep -q "CLIENT: success" %t3.log
// RUN: grep -q "SERVER: success" %t3.log
// RUN: not grep -q "error" %t3.log
// RUN: rm -rf %t4.dir
// RUN: %klee --output-dir=%t4.dir -cliver -enable-lazy-constraints -optimize=0 -posix-runtime -libc=uclibc -socket-log=%t1.ktest %t1.bc -i -P -Lp &> %t4.log
// RUN: grep -q "Verifier Result: success" %t4.log

// Negative test case: client sends 314, 6411
// RUN: %t1 -i -t -n > %t5.log
// RUN: grep -q "CLIENT: success" %t5.log
// RUN: grep -q "SERVER: success" %t5.log
// RUN: not grep -q "error" %t5.log
// RUN: rm -rf %t6.dir
// RUN: not %klee --output-dir=%t6.dir -cliver -enable-lazy-constraints -optimize=0 -posix-runtime -libc=uclibc -socket-log=%t1.ktest %t1.bc -i -P -Lp &> %t6.log
// RUN: not grep -q "Verifier Result: success" %t6.log

#include "TestLazyConstraint.inc.c"
