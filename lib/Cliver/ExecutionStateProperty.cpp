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

PathProperty::PathProperty() 
	: round(0),
	  phase(PathProperty::PrepareExecute) {}

int PathProperty::compare(const ExecutionStateProperty &b) const {
	const PathProperty *_b = static_cast<const PathProperty*>(&b);

	if (round != _b->round)
		return round - _b->round;

	if (phase != _b->phase)
		return phase - _b->phase;

	return path_range.compare(_b->path_range);
}

void PathProperty::print(std::ostream &os) const {
	os << "[round: " << round
	   << ", range: " << path_range
	   //<< ", trainingstate: " << phase
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
		case VerifyWithTrainingPaths: 
			return new PathProperty();
	}
	cv_error("invalid cliver mode");
	return NULL;
}

////////////////////////////////////////////////////////////////////////////////

} // End cliver namespace

