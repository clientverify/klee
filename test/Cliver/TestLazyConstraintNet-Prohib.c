// RUN: %llvmgcc -B/usr/lib/x86_64-linux-gnu %s "%srcroot/lib/Basic/KTest.cpp" -DKTEST=\"%t1.ktest\" -o %t1
// RUN: %llvmgcc %s -DKLEE -DCLIENT -emit-llvm -g -c -o %t1.bc

// Second set: prohibitive functions (should sometimes erroneously accept)

// Positive test case: client sends 6410, 10
// RUN: %t1 > %t1.log
// RUN: grep -q "CLIENT: success" %t1.log
// RUN: grep -q "SERVER: success" %t1.log
// RUN: not grep -q "error" %t1.log
// RUN: rm -rf %t2.dir
// RUN: %klee --output-dir=%t2.dir -cliver -optimize=0 -posix-runtime -libc=uclibc -socket-log=%t1.ktest %t1.bc -P &> %t2.log
// RUN: grep -q "Verifier Result: success" %t2.log

// Negative test case: client sends 6410, 11 (expect erroneous accept)
// RUN: %t1 -n > %t3.log
// RUN: grep -q "CLIENT: success" %t3.log
// RUN: grep -q "SERVER: success" %t3.log
// RUN: not grep -q "error" %t3.log
// RUN: rm -rf %t4.dir
// RUN: %klee --output-dir=%t4.dir -cliver -optimize=0 -posix-runtime -libc=uclibc -socket-log=%t1.ktest %t1.bc -P &> %t4.log
// RUN: grep -q "Verifier Result: success" %t4.log  # expect erroneous accept

// Third set: prohibitive functions with lazy constraint generators (should
// follow expected verifier result)

#include "TestLazyConstraint.inc.c"
