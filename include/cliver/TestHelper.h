//===-- TestHelper.h --------------------------------------------*- C++ -*-===//
//
// <insert license>
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//
#ifndef CLIVER_TESTHELPER_H
#define CLIVER_TESTHELPER_H

#include <vector>

namespace klee {
  class Executor;
  class Expr;
  class ExecutionState;
  struct KInstruction;
  template<typename T> class ref;
}
 
namespace cliver {

void ExternalHandler_test_extract_pointers(klee::Executor* executor,
		klee::ExecutionState *state, klee::KInstruction *target, 
    std::vector<klee::ref<klee::Expr> > &arguments);

class CVExecutor;

class	TestHelper {
 public:

	TestHelper(CVExecutor *executor) : executor_(executor) {}

 private:
	CVExecutor* executor_;
};

} // End cliver namespace

#endif 


