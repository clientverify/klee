//===-- CVExecutionState.cpp ------------------------------------*- C++ -*-===//
//
// <insert license>
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//
#include "CVExecutionState.h"
#include "../Core/Common.h"

namespace cliver {

int CVExecutionState::next_id_ = 0;
uint64_t kHeapStartAddress = 0xFF;

CVExecutionState::CVExecutionState(klee::KFunction *kF, klee::MemoryManager *mem) 
 : klee::ExecutionState(kF), id_(increment_id()), 
   memory_(static_cast<CVMemoryManager*>(mem)) {
  initialize();
}

//CVExecutionState::CVExecutionState(
//  const std::vector< klee::ref<klee::Expr> > &assumptions) {
//  assert(0);
//}

CVExecutionState::~CVExecutionState() {
  while (!stack.empty()) popFrame();
}

void CVExecutionState::initialize() {
  id_ = increment_id();
  coveredNew = false;
  coveredLines.clear();
  address_manager_ = memory_->create_address_manager(this, NULL);
}

CVExecutionState* CVExecutionState::branch() {
  depth++;

  CVExecutionState *falseState = new CVExecutionState(*this);
  falseState->id_ = increment_id();
  falseState->coveredNew = false;
  falseState->coveredLines.clear();
  falseState->address_manager_ 
    = memory_->create_address_manager(falseState, address_manager_);

  weight *= .5;
  falseState->weight -= weight;

  return falseState;
}

} // End cliver namespace

