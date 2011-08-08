//===-- CVExecutionState.cpp ------------------------------------*- C++ -*-===//
//
// <insert license>
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//
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

TrainingProperty::TrainingProperty() 
	: training_round(0),
	  training_state(TrainingProperty::Execute),
		start_instruction_id(0),
		end_instruction_id(0) {}

void TrainingProperty::handle_post_event(CVExecutionState *state,
		CliverEvent::Type et) {

	TrainingProperty* p = static_cast<TrainingProperty*>(state->property());

	switch(p->training_state) {
		case TrainingProperty::PrepareExecute:
			break;
		case TrainingProperty::Execute:
			if (et == CliverEvent::Training) {
				p->training_state = TrainingProperty::Record;
			}
			break;
		case TrainingProperty::PrepareNetworkClone:
			break;
		case TrainingProperty::NetworkClone:
			if (et == CliverEvent::Network) {
				p->training_state = TrainingProperty::Record;
			}
			break;
		case TrainingProperty::Record:
			break;
	}

}

void TrainingProperty::handle_pre_event(CVExecutionState *state,
		CliverEvent::Type et) {

	TrainingProperty* p = static_cast<TrainingProperty*>(state->property());

	switch(p->training_state) {
		case TrainingProperty::PrepareExecute:
			break;
		case TrainingProperty::Execute:
			if (et == CliverEvent::Network) {
				p->training_state = TrainingProperty::PrepareNetworkClone;
			}
			break;
		case TrainingProperty::PrepareNetworkClone:
			break;
		case TrainingProperty::NetworkClone:
			break;
		case TrainingProperty::Record:
			break;
	}
}

int TrainingProperty::compare(const ExecutionStateProperty &b) const {
	const TrainingProperty *_b = static_cast<const TrainingProperty*>(&b);
	cv_error("fixme");
	return training_state - _b->training_state;
}

void TrainingProperty::print(std::ostream &os) const {
	cv_error("fixme");
	os << "training phase = " << training_state;
}

////////////////////////////////////////////////////////////////////////////////


TrainingPhaseProperty::TrainingPhaseProperty() : training_phase(0) {}

void TrainingPhaseProperty::handle_post_event(CVExecutionState *state,
		CliverEvent::Type et) {}

void TrainingPhaseProperty::handle_pre_event(CVExecutionState *state,
		CliverEvent::Type et) {

	TrainingPhaseProperty* p = static_cast<TrainingPhaseProperty*>(state->property());

	assert(p->training_phase != 1);
	assert(p->training_phase != 3);
	assert(p->training_phase != 5);
	assert(p->training_phase != 7);

	if (et == CliverEvent::Network) {
		switch(p->training_phase) {
			case 0:
				// Branch a new state for every log
				p->training_phase = 1;
				break;
			case 4:
				// Spawn a state for every event
				p->training_phase = 5;
				break;
		}
	} else if (et == CliverEvent::Training) {
		switch(p->training_phase) {
			case 2:
				// Merge all states
				p->training_phase = 3;
				break;
			case 6:
				// Write path to file
				p->training_phase = 7;
				break;
		}
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
	case TetrinetMode: 
		return new LogIndexProperty();
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
	network_manager_ = NetworkManagerFactory::create(this);
	path_manager_ = PathManagerFactory::create();
	property_ = ExecutionStatePropertyFactory::create();

	foreach (KTest* ktest, executor->client_verifier()->socket_logs()) {
		network_manager_->add_socket(ktest);
	}
}

CVExecutionState* CVExecutionState::clone() {
  CVExecutionState *cloned_state = new CVExecutionState(*this);
  cloned_state->id_ = increment_id();
  cloned_state->network_manager_ 
		= network_manager_->clone(cloned_state); 
	cloned_state->path_manager_ = path_manager_->clone();
  cloned_state->property_ = property_->clone();

  return cloned_state;
}

CVExecutionState* CVExecutionState::branch() {
  depth++;
  CVExecutionState *false_state = clone();
  false_state->coveredNew = false;
  false_state->coveredLines.clear();
  weight *= .5;
  false_state->weight -= weight;
  return false_state;
}

bool CVExecutionStateLT::operator()(const CVExecutionState* a, 
		const CVExecutionState* b) const {
	return a->compare(*b) < 0;
}

} // End cliver namespace

