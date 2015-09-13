// RUN: %llvmgcc -B/usr/lib/x86_64-linux-gnu %s "../../lib/Basic/KTest.cpp" -DKTEST=\"%t1.ktest\" -o %t1
// RUN: %llvmgcc %s -DKLEE -DCLIENT -emit-llvm -g -c -o %t1.bc 
// RUN: %t1 > %t1.log
// RUN: grep -q "CLIENT: success" %t1.log
// RUN: grep -q "SERVER: success" %t1.log
// RUN: not grep -q "error" %t1.log
// RUN: rm -rf %t1.dir
// RUN: %klee --output-dir=%t1.dir -cliver -optimize=0 -posix-runtime -libc=uclibc -socket-log=%t1.ktest %t1.bc &> %t1.log
// RUN: grep -q "CLIENT: success" %t1.log
// RUN: rm -rf %t2.dir
// RUN: %klee -cliver -cliver-mode=training -optimize=0 -posix-runtime -libc=uclibc -output-dir=%t2.dir -socket-log=%t1.ktest %t1.bc &> %t2.log
// RUN: grep -q "CLIENT: success" %t2.log
// RUN: rm -rf %t3.dir
// RUN: %klee -cliver -use-clustering -cliver-mode=edit-dist-kprefix-row -optimize=0 -posix-runtime -libc=uclibc -output-dir=%t3.dir -training-path-dir=%t2.dir -socket-log=%t1.ktest %t1.bc &> %t3.log
// RUN: not grep -q "Recomputed kprefix" %t3.log
// RUN: grep -q "CLIENT: success" %t3.log
// RUN: %klee -use-self-training  -basic-block-event-flag=1 --use-threads=1 -cliver -use-clustering -cliver-mode=edit-dist-kprefix-row -optimize=0 -posix-runtime -libc=uclibc -output-dir=%t4.dir -self-training-path-dir=%t2.dir -socket-log=%t1.ktest %t1.bc &> %t4.log
//// Check that we did not execute more instructions than necessary (less than 0.5% extra instructions)
// RUN: awk -F, '{total[$1]=$4} END { y=total["InstructionCount"]; x=total["ValidPathInstructionCount"]; exit !(0.01 > (y-x)/y);  }' %t4.dir/cliver.stats.summary
// RUN: not grep -q "Self Training Data Mismatch" %t4.log

#include "ClientServer.c"

