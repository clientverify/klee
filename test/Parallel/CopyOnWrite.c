// RUN: %llvmgcc %s -emit-llvm -g -c -o %t1.bc
// RUN: rm -rf %t.klee-out
// RUN: %klee --use-batching-search=1 --use-forked-solver=0 -output-istats=0 --use-call-paths=0 -use-threads=24 --output-dir=%t.klee-out --search=bfs --exit-on-error %t1.bc
// REQUIRES: threads

#include <assert.h>

#define N 5

unsigned branches = 0;

void explode(int *ap, int i, int *result) {
  if (i<N) {
    (*result)++;
    if (ap[i]) // just cause a fork
      branches++; 
    return explode(ap, i+1, result);
  }
}

int main() {
  int result = 0;
  int a[N];
  klee_make_symbolic(a, sizeof a);
  explode(a,0,&result);
  assert(result==N);
  return 0;
}
