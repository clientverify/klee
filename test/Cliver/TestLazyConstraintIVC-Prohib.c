// RUN: %llvmgcc -B/usr/lib/x86_64-linux-gnu %s "%srcroot/lib/Basic/KTest.cpp" -DKTEST=\"%t1.ktest\" -o %t1
// RUN: %llvmgcc %s -DKLEE -DCLIENT -emit-llvm -g -c -o %t1.bc

// Second set: prohibitive functions (should sometimes erroneously accept)

// Positive test case: client sends 314, 159
// RUN: %t1 -i > %t1.log
// RUN: grep -q "CLIENT: success" %t1.log
// RUN: grep -q "SERVER: success" %t1.log
// RUN: not grep -q "error" %t1.log
// RUN: rm -rf %t2.dir
// RUN: %klee --output-dir=%t2.dir -cliver -optimize=0 -posix-runtime -libc=uclibc -socket-log=%t1.ktest %t1.bc -i -P &> %t2.log
// RUN: grep -q "Verifier Result: success" %t2.log

// Positive test case: client sends 314, 6410
// RUN: %t1 -i -t > %t3.log
// RUN: grep -q "CLIENT: success" %t3.log
// RUN: grep -q "SERVER: success" %t3.log
// RUN: not grep -q "error" %t3.log
// RUN: rm -rf %t4.dir
// RUN: %klee --output-dir=%t4.dir -cliver -optimize=0 -posix-runtime -libc=uclibc -socket-log=%t1.ktest %t1.bc -i -P &> %t4.log
// RUN: grep -q "Verifier Result: success" %t4.log

// Negative test case: client sends 314, 6411 (expect erroneous accept)
// RUN: %t1 -i -t -n > %t5.log
// RUN: grep -q "CLIENT: success" %t5.log
// RUN: grep -q "SERVER: success" %t5.log
// RUN: not grep -q "error" %t5.log
// RUN: rm -rf %t6.dir
// RUN: %klee --output-dir=%t6.dir -cliver -optimize=0 -posix-runtime -libc=uclibc -socket-log=%t1.ktest %t1.bc -i -P &> %t6.log
// RUN: grep -q "Verifier Result: success" %t6.log # expect erroneous accept

// Third set: prohibitive functions and lazy constraint generators (should
// follow expected verifier result)

#include "TestLazyConstraint.inc.c"
