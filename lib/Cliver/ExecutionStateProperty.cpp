//===-- ExecutionStateProperty.cpp ------------------------------*- C++ -*-===//
//
// <insert license>
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//

#include "ExecutionStateProperty.h"
#include "ClientVerifier.h"

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
	  training_state(TrainingProperty::PrepareExecute) {}

int TrainingProperty::compare(const ExecutionStateProperty &b) const {
	const TrainingProperty *_b = static_cast<const TrainingProperty*>(&b);

	if (training_round != _b->training_round)
		return training_round - _b->training_round;

	if (training_state != _b->training_state)
		return training_state - _b->training_state;

	return path_range.compare(_b->path_range);
}

void TrainingProperty::print(std::ostream &os) const {
	os << "[round: " << training_round
	   << ", range: " << path_range
	   //<< ", trainingstate: " << training_state
		 << "]";
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

////////////////////////////////////////////////////////////////////////////////

} // End cliver namespace

