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
	: round(-1), client_round(0), edit_distance(0), 
    symbolic_vars(0), recompute(true), is_recv_processing(false) {}

ExecutionStateProperty* ExecutionStateProperty::clone() { 
  ExecutionStateProperty* esp = new ExecutionStateProperty(*this);
  esp->round = round;
  esp->client_round = client_round;
  esp->edit_distance = edit_distance;
  esp->symbolic_vars = symbolic_vars;
  esp->recompute = true;
  esp->is_recv_processing = is_recv_processing;
  return esp;
}

void ExecutionStateProperty::reset() {
  symbolic_vars = 0;
  edit_distance = 0;

  //if (is_recv_processing) {
  //  CVMESSAGE("Resetting is_recv_processing");
  //  is_recv_processing = false;
  //}
}

// Order by greatest round number, then smallest edit distance
int ExecutionStateProperty::compare(const ExecutionStateProperty &b) const {
	const ExecutionStateProperty *_b = static_cast<const ExecutionStateProperty*>(&b);

	if (_b->is_recv_processing != is_recv_processing) {
    return (char)_b->is_recv_processing - (char)is_recv_processing;
  }

  // Reversed for priority queue!
	if (_b->edit_distance != edit_distance) {
    return _b->edit_distance - edit_distance;
  }

	if (round != _b->round)
		return round - _b->round;

	if (client_round != _b->client_round)
		return client_round - _b->client_round;
  
  // Reversed for priority queue!
	if (_b->symbolic_vars != symbolic_vars)
		return _b->symbolic_vars - symbolic_vars;

  return 0;
}

void ExecutionStateProperty::print(std::ostream &os) const {
	os << "[rd: " << round << "]";
  if (client_round > 0)
    os << "[clrd: " << client_round << "]";
  if (edit_distance >= 0)
	  os << "[ed: " << edit_distance << "]";
  if (symbolic_vars >= 0)
	  os << "[sv: " << symbolic_vars << "]";
  if (!recompute)
	  os << "[NoRC]";
}

//////////////////////////////////////////////////////////////////////////////

EditDistanceExecutionStateProperty::EditDistanceExecutionStateProperty() {}

// Only compare edit distance
int EditDistanceExecutionStateProperty::compare(
    const ExecutionStateProperty &b) const {

	const EditDistanceExecutionStateProperty *_b 
      = static_cast<const EditDistanceExecutionStateProperty*>(&b);

	if (_b->is_recv_processing != is_recv_processing) {
    return (char)_b->is_recv_processing - (char)is_recv_processing;
  } else if (_b->is_recv_processing == true &&
             is_recv_processing == true) {

    if (round != _b->round)
      return round - _b->round;

    if (client_round != _b->client_round)
      return client_round - _b->client_round;
    
    // Reversed for priority queue!
    if (_b->symbolic_vars != symbolic_vars)
      return _b->symbolic_vars - symbolic_vars;
  }

  // Reversed for priority queue!
	if (_b->edit_distance != edit_distance) {
    return _b->edit_distance - edit_distance;
  }

  // Reversed for priority queue!
  if (_b->symbolic_vars != symbolic_vars)
    return _b->symbolic_vars - symbolic_vars;

  return 0;
}


//	//if (_b->is_recv_processing != is_recv_processing)
//  //  return (int)_b->is_recv_processing - (int)is_recv_processing;
//
//  //// Reversed for priority queue!
//	//if (_b->edit_distance != edit_distance) 
//  //  return _b->edit_distance - edit_distance;
//
//	if (_b->is_recv_processing && is_recv_processing) {
//    if (client_round != _b->client_round)
//      return client_round - _b->client_round;
//
//    // Reversed for priority queue!
//    if (_b->symbolic_vars != symbolic_vars)
//      return _b->symbolic_vars - symbolic_vars;
//  } else {
//    // Reversed for priority queue!
//    if (_b->edit_distance != edit_distance) 
//      return _b->edit_distance - edit_distance;
//  }
//
//  return 0;
//}

////////////////////////////////////////////////////////////////////////////////

} // End cliver namespace

