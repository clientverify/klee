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
#include "PathManager.h"
#include "ClientVerifier.h"
#include "../Core/Common.h"

#include <boost/foreach.hpp>
#define foreach BOOST_FOREACH 

namespace cliver {
	
////////////////////////////////////////////////////////////////////////////////

bool ExecutionStatePropertyLT::operator()(const ExecutionStateProperty* a, 
		const ExecutionStateProperty* b) const {
	return a->compare(*b) < 0;
}

////////////////////////////////////////////////////////////////////////////////

LogIndexProperty::LogIndexProperty() : socket_log_index(-1) {}

void LogIndexProperty::handle_pre_event(CVExecutionState *state,
		CliverEvent::Type et) {}

void LogIndexProperty::handle_post_event(CVExecutionState *state,
		CliverEvent::Type et) {

	LogIndexProperty* p = static_cast<LogIndexProperty*>(state->property());
	if(et == CliverEvent::Network) {
		p->socket_log_index = state->network_manager()->socket_log_index();
	}
}

int LogIndexProperty::compare(const ExecutionStateProperty &b) const {
	const LogIndexProperty *_b = static_cast<const LogIndexProperty*>(&b);
	return socket_log_index - _b->socket_log_index;
}

void LogIndexProperty::print(std::ostream &os) const {
	os << "log index = " << socket_log_index;
}

//////////////////////////////////////////////////////////////////////////////

TrainingPhaseProperty::TrainingPhaseProperty() : training_phase(0) {}

void TrainingPhaseProperty::handle_post_event(CVExecutionState *state,
		CliverEvent::Type et) {}

void TrainingPhaseProperty::handle_pre_event(CVExecutionState *state,
		CliverEvent::Type et) {

	TrainingPhaseProperty* p = static_cast<TrainingPhaseProperty*>(state->property());
	switch(et) {

		case CliverEvent::Network:
			if (p->training_phase == 0) {
				// Branch a new state for every log
				p->training_phase = 1;

			} else if (p->training_phase == 2) {
				// Branch a new state for every socket event 
				p->training_phase = 3;

			} else if (p->training_phase == 3) {
				p->training_phase = 4;
			}
			break;

		case CliverEvent::Training:
			if (p->training_phase == 1) {
				// Merge all states
				p->training_phase = 2;
			}
			break;
	}
}

int TrainingPhaseProperty::compare(const ExecutionStateProperty &b) const {
	const TrainingPhaseProperty *_b = static_cast<const TrainingPhaseProperty*>(&b);
	return training_phase - _b->training_phase;
}

void TrainingPhaseProperty::print(std::ostream &os) const {
	os << "training phase = " << training_phase;
}

////////////////////////////////////////////////////////////////////////////////

ExecutionStateProperty* ExecutionStatePropertyFactory::create() {
  switch (g_cliver_mode) {
	case DefaultMode:
		return new LogIndexProperty();
    break;
	case TetrinetMode: 
		break;
	case DefaultTrainingMode: 
		return new TrainingPhaseProperty();
  }
	cv_error("invalid cliver mode");
  return NULL;
}

////////////////////////////////////////////////////////////////////////////////

int CVExecutionState::next_id_ = 0;

CVExecutionState::CVExecutionState(klee::KFunction *kF, klee::MemoryManager *mem)
 : klee::ExecutionState(kF, mem), 
	 id_(increment_id()) {}

CVExecutionState::CVExecutionState(
    const std::vector< klee::ref<klee::Expr> > &assumptions)
    : klee::ExecutionState(assumptions) {
  cv_error("Not supported.");
}

CVExecutionState::~CVExecutionState() {
  while (!stack.empty()) popFrame();
	//delete address_manager_;
	delete network_manager_;
	delete path_manager_;
	delete property_;
}

int CVExecutionState::compare(const CVExecutionState& b) const {
	return property_->compare(*b.property_);
}


void CVExecutionState::initialize(CVExecutor *executor) {
  id_ = increment_id();
  coveredNew = false;
  coveredLines.clear();
  //address_manager_ = AddressManagerFactory::create(this);
	network_manager_ = NetworkManagerFactory::create(this);
	path_manager_ = PathManagerFactory::create();
	property_ = ExecutionStatePropertyFactory::create();

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

  //falseState->address_manager_ = address_manager_->clone(); 
  //falseState->address_manager_->set_state(falseState);

  falseState->network_manager_ = network_manager_->clone(falseState); 
	falseState->path_manager_ = path_manager_->clone();
  falseState->property_ = property_->clone();

  weight *= .5;
  falseState->weight -= weight;

  return falseState;
}

bool CVExecutionStateLT::operator()(const CVExecutionState* a, 
		const CVExecutionState* b) const {
	return a->compare(*b) < 0;
}

} // End cliver namespace

