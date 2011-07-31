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

ExecutionStateInfo::ExecutionStateInfo(CVExecutionState* state) {
	update(state);
}

void ExecutionStateInfo::update(CVExecutionState* state) {
//	socket_log_index_ = 0;
	if (state) {
		if (state->network_manager()) {
			if (state->network_manager()->sockets().size() > 0) {
				socket_log_index_ = state->network_manager()->sockets().back().index();
			}
		}
	}
}

bool ExecutionStateInfo::less_than(const ExecutionStateInfo &info) const {
	return socket_log_index_ < info.socket_log_index_;
}

bool ExecutionStateInfo::equals(const ExecutionStateInfo &info) const {
	return socket_log_index_ == info.socket_log_index_;
}

bool ExecutionStateInfoLT::operator()(const ExecutionStateInfo &a, 
		const ExecutionStateInfo &b) const {
	return a.less_than(b);
}

////////////////////////////////////////////////////////////////////////////////

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
	delete address_manager_;
	delete network_manager_;
	delete info_;
}

void CVExecutionState::initialize(CVExecutor *executor) {
  id_ = increment_id();
  coveredNew = false;
  coveredLines.clear();
  address_manager_ = AddressManagerFactory::create(this);
	network_manager_ = NetworkManagerFactory::create(this);
	info_ = new ExecutionStateInfo(this);

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
  falseState->info_ = new ExecutionStateInfo(falseState);

  weight *= .5;
  falseState->weight -= weight;

  return falseState;
}

} // End cliver namespace

