//===-- ExecutionStateProperty.cpp ------------------------------*- C++ -*-===//
//
// <insert license>
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//
#include "cliver/ExecutionStateProperty.h"
#include "cliver/ClientVerifier.h"
#include "cliver/CVExecutionState.h"
#include "cliver/CVStream.h"
#include "CVCommon.h"

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

//////////////////////////////////////////////////////////////////////////////

VerifyProperty::VerifyProperty() 
	: round(0),
	  phase(VerifyProperty::Execute) {}

int VerifyProperty::compare(const ExecutionStateProperty &b) const {
	const VerifyProperty *_b = static_cast<const VerifyProperty*>(&b);

	if (round != _b->round)
		return round - _b->round;

	if (phase != _b->phase)
		return phase - _b->phase;

	return path_range.compare(_b->path_range);
}

void VerifyProperty::print(std::ostream &os) const {
	os << "[round: " << round
	   << ", range: " << path_range
	   << ", phase: " << phase
		 << "]";
}

//////////////////////////////////////////////////////////////////////////////

EditCostProperty::EditCostProperty() 
	: edit_cost(rand()/(double)RAND_MAX) {}

EditCostProperty* EditCostProperty::clone() { 
  EditCostProperty* ecp = new EditCostProperty(*this);
  ecp->edit_cost = rand()/(double)RAND_MAX;
  return ecp;
}

int EditCostProperty::compare(const ExecutionStateProperty &b) const {
	const EditCostProperty *_b = static_cast<const EditCostProperty*>(&b);

  if (edit_cost > _b->edit_cost)
    return 1;
  else if (edit_cost < _b->edit_cost)
    return -1;
  return 0;
}

void EditCostProperty::print(std::ostream &os) const {
	os << "[edit cost: " << edit_cost
		 << "]";
}

//////////////////////////////////////////////////////////////////////////////

EditDistanceProperty::EditDistanceProperty() 
	: edit_distance(INT_MAX-1), recompute(true) {}

EditDistanceProperty* EditDistanceProperty::clone() { 
  EditDistanceProperty* ecp = new EditDistanceProperty(*this);
  ecp->edit_distance = edit_distance;
  ecp->recompute = true;
  return ecp;
}

int EditDistanceProperty::compare(const ExecutionStateProperty &b) const {
	const EditDistanceProperty *_b = static_cast<const EditDistanceProperty*>(&b);

  // REVERSED FOR PRIORITY QUEUE!!!
  return _b->edit_distance - edit_distance;
  //if (edit_distance > _b->edit_distance)
  //  return 1;
  //else if (edit_distance < _b->edit_distance)
  //  return -1;
  //return 0;
}

void EditDistanceProperty::print(std::ostream &os) const {
	os << "[edit distance: " << edit_distance
		 << "]";
}

//////////////////////////////////////////////////////////////////////////////

NumSymbolicVarsProperty::NumSymbolicVarsProperty() 
	: num_symbolic_vars(0) {}

NumSymbolicVarsProperty* NumSymbolicVarsProperty::clone() { 
  NumSymbolicVarsProperty* cp = new NumSymbolicVarsProperty(*this);
  cp->num_symbolic_vars = num_symbolic_vars;
  return cp;
}

int NumSymbolicVarsProperty::compare(const ExecutionStateProperty &b) const {
	const NumSymbolicVarsProperty *_b = static_cast<const NumSymbolicVarsProperty*>(&b);

  // REVERSED FOR USE IN PRIORITY QUEUE!!!
  return _b->num_symbolic_vars - num_symbolic_vars;
}

void NumSymbolicVarsProperty::print(std::ostream &os) const {
	os << "[edit distance: " << num_symbolic_vars
		 << "]";
}

////////////////////////////////////////////////////////////////////////////////

} // End cliver namespace

