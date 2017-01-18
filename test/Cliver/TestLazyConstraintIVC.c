// RUN: %llvmgcc -B/usr/lib/x86_64-linux-gnu %s "%srcroot/lib/Basic/KTest.cpp" -DKTEST=\"%t1.ktest\" -o %t1
// RUN: %llvmgcc %s -DKLEE -DCLIENT -emit-llvm -g -c -o %t1.bc
// RUN: %t1 -i > %t1.log
// RUN: grep -q "CLIENT: success" %t1.log
// RUN: grep -q "SERVER: success" %t1.log
// RUN: not grep -q "error" %t1.log
// RUN: rm -rf %t2.dir
// RUN: %klee --output-dir=%t2.dir -cliver -optimize=0 -posix-runtime -libc=uclibc -socket-log=%t1.ktest %t1.bc -i &> %t2.log
// RUN: grep -q "CLIENT: success" %t2.log
// RUN: %t1 -i -t > %t3.log
// RUN: grep -q "CLIENT: success" %t3.log
// RUN: grep -q "SERVER: success" %t3.log
// RUN: not grep -q "error" %t3.log
// RUN: rm -rf %t4.dir
// RUN: %klee --output-dir=%t4.dir -cliver -optimize=0 -posix-runtime -libc=uclibc -socket-log=%t1.ktest %t1.bc -i &> %t4.log
// RUN: grep -q "CLIENT: success" %t4.log
// RUN: %t1 -i -t -n > %t5.log
// RUN: grep -q "CLIENT: success" %t5.log
// RUN: grep -q "SERVER: success" %t5.log
// RUN: not grep -q "error" %t5.log
// RUN: rm -rf %t6.dir
// RUN: not %klee --output-dir=%t6.dir -cliver -optimize=0 -posix-runtime -libc=uclibc -socket-log=%t1.ktest %t1.bc -i &> %t6.log
// RUN: not grep -q "CLIENT: success" %t6.log

#include "LazyConstraint.c"
