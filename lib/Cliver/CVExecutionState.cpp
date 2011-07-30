//===-- CVExecutionState.cpp ------------------------------------*- C++ -*-===//
//
// <insert license>
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//
#include "SharedObjects.h"
#include "CVExecutionState.h"
#include "CVExecutor.h"
#include "CVStream.h"
#include "NetworkManager.h"
#include "../Core/Common.h"

#include <boost/foreach.hpp>
#define foreach BOOST_FOREACH 

namespace cliver {

int CVExecutionState::next_id_ = 0;

CVExecutionState::CVExecutionState(klee::KFunction *kF)
 : klee::ExecutionState(kF), 
	 id_(increment_id()) {}

CVExecutionState::CVExecutionState(
    const std::vector< klee::ref<klee::Expr> > &assumptions)
    : klee::ExecutionState(assumptions) {
  cv_error("Not supported.");
}

CVExecutionState::~CVExecutionState() {
  while (!stack.empty()) popFrame();
}

void CVExecutionState::initialize(CVExecutor *executor) {
  id_ = increment_id();
  coveredNew = false;
  coveredLines.clear();
  address_manager_ = AddressManagerFactory::create(this);
	network_manager_ = NetworkManagerFactory::create(this);

	foreach (KTest* ktest, executor->client_verifier()->socket_logs()) {
		network_manager_->add_socket(ktest);
	}
}

CVExecutionState* CVExecutionState::branch() {
  depth++;

  CVExecutionState *falseState = new CVExecutionState(*this);
  falseState->id_ = increment_id();
  falseState->coveredNew = false;
  falseState->coveredLines.clear();
  falseState->address_manager_ = address_manager_->clone(); 
  falseState->address_manager_->set_state(falseState);
  falseState->network_manager_ = network_manager_->clone(falseState); 

  weight *= .5;
  falseState->weight -= weight;

  return falseState;
}

} // End cliver namespace

