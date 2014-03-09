// RUN: %llvmgcc %s -emit-llvm -g -c -o %t1.bc
// RUN: %klee -cliver -libc=klee -posix-runtime -all-external-warnings %t1.bc > %t.log

// RUN: grep -q "PASSED" %t.log

//#include <stdio.h>
//#include <stdlib.h>

void cliver_test_extract_pointers();

typedef struct _LLNode {
  unsigned data;
  struct _LLNode *next;;
} LLNode;

int main(int argc, char **argv) {
  unsigned i;
  LLNode node1, node2;
  node1.data = 8;
  node2.data = 8;
  node1.next = &node2;
  node2.next = &node1;
  //printf("Node1.next = %x\n", node1.next);
	cliver_test_extract_pointers();
  return 0;
}
