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

//////////////////////////////////////////////////////////////////////////////

ExecutionStateProperty::ExecutionStateProperty()
	: round(-1), edit_distance(0), recompute(true) {}

ExecutionStateProperty* ExecutionStateProperty::clone() { 
  ExecutionStateProperty* esp = new ExecutionStateProperty(*this);
  esp->round = round;
  esp->edit_distance = edit_distance;
  esp->recompute = true;
  return esp;
}

int ExecutionStateProperty::compare(const ExecutionStateProperty &b) const {
	const ExecutionStateProperty *_b = static_cast<const ExecutionStateProperty*>(&b);

	if (round != _b->round)
		return round - _b->round;

  // REVERSED FOR PRIORITY QUEUE!!!
  return _b->edit_distance - edit_distance;

  return 0;
}

void ExecutionStateProperty::print(std::ostream &os) const {
	os << "[rd: " << round << "]"
	   << "[edit distance: " << edit_distance << "]";
}

////////////////////////////////////////////////////////////////////////////////

} // End cliver namespace

