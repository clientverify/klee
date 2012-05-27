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
	: round(-1), client_round(0), edit_distance(-1), recompute(true) {}

ExecutionStateProperty* ExecutionStateProperty::clone() { 
  ExecutionStateProperty* esp = new ExecutionStateProperty(*this);
  esp->round = round;
  esp->client_round = client_round;
  esp->edit_distance = edit_distance;
  esp->recompute = true;
  return esp;
}

// Order by greatest round number, then smallest edit distance
int ExecutionStateProperty::compare(const ExecutionStateProperty &b) const {
	const ExecutionStateProperty *_b = static_cast<const ExecutionStateProperty*>(&b);

	if (round != _b->round)
		return round - _b->round;

	if (client_round != _b->client_round)
		return client_round - _b->client_round;

  // Reversed for priority queue!
  return _b->edit_distance - edit_distance;

  return 0;
}

void ExecutionStateProperty::print(std::ostream &os) const {
	os << "[rd: " << round << "]";
  if (client_round >= 0)
    os << "[clrd: " << round << "]";
  if (edit_distance >= 0)
	  os << "[ed: " << edit_distance << "]";
  if (!recompute)
	  os << "[NoRC]";
}

////////////////////////////////////////////////////////////////////////////////

} // End cliver namespace

