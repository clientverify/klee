// RUN: %llvmgxx %s -emit-llvm -O0 -c -o %t1.bc
// RUN: rm -rf %t.klee-out
// RUN: %klee --output-dir=%t.klee-out -cliver -libc=klee -all-external-warnings %t1.bc > %t
// RUN: grep -q "test_extract_pointers: PASSED" %t

#ifdef __cplusplus
extern "C" {
#endif
void cliver_test_extract_pointers();
#ifdef __cplusplus
}
#endif

#include <cassert>

static int decon = 0;

class Test {
  int x;

public:
  Test() {}
  Test(int _x) : x(_x) { }
  ~Test() { decon += x; }

  int getX() { return x; }
  void setX(int _x) { x = _x; }
};

int main(int argc) {
  Test *rt = new Test[4];
  int i;

  for (i=0; i<4; i++)
    rt[i].setX(i+1);

  int sum = 0;
  for (i=0; i<4; i++)
    sum += rt[i].getX();
  
  assert(sum==10);

  delete[] rt;

  assert(decon==10);

	cliver_test_extract_pointers();
  return 0;
}
