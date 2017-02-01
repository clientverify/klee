// RUN: %llvmgcc -B/usr/lib/x86_64-linux-gnu %s "%srcroot/lib/Basic/KTest.cpp" -DKTEST=\"%t1.ktest\" -o %t1
// RUN: %llvmgcc %s -DKLEE -DCLIENT -emit-llvm -g -c -o %t1.bc

// First set: fully concrete execution (should follow expected verifier result)

// Positive test case: client sends 314, 159
// RUN: %t1 -i > %t1.log
// RUN: grep -q "CLIENT: success" %t1.log
// RUN: grep -q "SERVER: success" %t1.log
// RUN: not grep -q "error" %t1.log
// RUN: rm -rf %t2.dir
// RUN: %klee --output-dir=%t2.dir -cliver -optimize=0 -posix-runtime -libc=uclibc -socket-log=%t1.ktest %t1.bc -i &> %t2.log
// RUN: grep -q "Verifier Result: success" %t2.log

// Positive test case: client sends 314, 6410
// RUN: %t1 -i -t > %t3.log
// RUN: grep -q "CLIENT: success" %t3.log
// RUN: grep -q "SERVER: success" %t3.log
// RUN: not grep -q "error" %t3.log
// RUN: rm -rf %t4.dir
// RUN: %klee --output-dir=%t4.dir -cliver -optimize=0 -posix-runtime -libc=uclibc -socket-log=%t1.ktest %t1.bc -i &> %t4.log
// RUN: grep -q "Verifier Result: success" %t4.log

// Negative test case: client sends 314, 6411
// RUN: %t1 -i -t -n > %t5.log
// RUN: grep -q "CLIENT: success" %t5.log
// RUN: grep -q "SERVER: success" %t5.log
// RUN: not grep -q "error" %t5.log
// RUN: rm -rf %t6.dir
// RUN: not %klee --output-dir=%t6.dir -cliver -optimize=0 -posix-runtime -libc=uclibc -socket-log=%t1.ktest %t1.bc -i &> %t6.log
// RUN: not grep -q "Verifier Result: success" %t6.log

// Second set: prohibitive functions (should sometimes erroneously accept)

// Positive test case: client sends 314, 159
// RUN: %t1 -i > %t7.log
// RUN: grep -q "CLIENT: success" %t7.log
// RUN: grep -q "SERVER: success" %t7.log
// RUN: not grep -q "error" %t7.log
// RUN: rm -rf %t8.dir
// RUN: %klee --output-dir=%t8.dir -cliver -optimize=0 -posix-runtime -libc=uclibc -socket-log=%t1.ktest %t1.bc -i -P &> %t8.log
// RUN: grep -q "Verifier Result: success" %t8.log

// Positive test case: client sends 314, 6410
// RUN: %t1 -i -t > %t9.log
// RUN: grep -q "CLIENT: success" %t9.log
// RUN: grep -q "SERVER: success" %t9.log
// RUN: not grep -q "error" %t9.log
// RUN: rm -rf %t10.dir
// RUN: %klee --output-dir=%t10.dir -cliver -optimize=0 -posix-runtime -libc=uclibc -socket-log=%t1.ktest %t1.bc -i -P &> %t10.log
// RUN: grep -q "Verifier Result: success" %t10.log

// Negative test case: client sends 314, 6411 (expect erroneous accept)
// RUN: %t1 -i -t -n > %t11.log
// RUN: grep -q "CLIENT: success" %t11.log
// RUN: grep -q "SERVER: success" %t11.log
// RUN: not grep -q "error" %t11.log
// RUN: rm -rf %t12.dir
// RUN: %klee --output-dir=%t12.dir -cliver -optimize=0 -posix-runtime -libc=uclibc -socket-log=%t1.ktest %t1.bc -i -P &> %t12.log
// RUN: grep -q "Verifier Result: success" %t12.log # expect erroneous accept

// Third set: prohibitive functions and lazy constraint generators (should
// follow expected verifier result)

#include "TestLazyConstraint.inc.c"
