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
#include "llvm/Support/raw_ostream.h"

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
	  training_state(TrainingProperty::PrepareExecute),
		start_instruction_id(0),
		end_instruction_id(0) {}

int TrainingProperty::compare(const ExecutionStateProperty &b) const {
	const TrainingProperty *_b = static_cast<const TrainingProperty*>(&b);

	if (0 != training_round - _b->training_round)
		return training_round - _b->training_round;

	if (0 != training_state - _b->training_state)
		return training_state - _b->training_state;

	if (0 != start_instruction_id - _b->start_instruction_id)
		return start_instruction_id - _b->start_instruction_id;

	if (0 != end_instruction_id - _b->end_instruction_id)
		return end_instruction_id - _b->end_instruction_id;
}

void TrainingProperty::print(std::ostream &os) const {
	os << "training [round= " << training_round
	   << ", state= " << training_state
	   << ", start_inst_id = " << start_instruction_id
	   << ", end_inst_id = " << end_instruction_id;
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

void CVExecutionState::get_pc_string(std::string &rstr) {
	llvm::raw_string_ostream ros(rstr);
	ros << *(pc->inst);
	rstr.erase(std::remove(rstr.begin(), rstr.end(), '\n'), rstr.end());
	ros.flush();
}

void CVExecutionState::initialize(CVExecutor *executor) {
  id_ = increment_id();
  coveredNew = false;
  coveredLines.clear();
	network_manager_ = NetworkManagerFactory::create(this);
	path_manager_ = PathManagerFactory::create();
	property_ = ExecutionStatePropertyFactory::create();
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

////////////////////////////////////////////////////////////////////////////////

ExecutionStateProperty* ExecutionStatePropertyFactory::create() {
	switch (g_cliver_mode) {
		case DefaultMode:
		case TetrinetMode: 
			return new LogIndexProperty();
			break;
		case DefaultTrainingMode: 
			return new TrainingProperty();
	}
	cv_error("invalid cliver mode");
	return NULL;
}

} // End cliver namespace

