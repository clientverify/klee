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
	return a->compare(b) < 0;
}

//////////////////////////////////////////////////////////////////////////////

ExecutionStateProperty::ExecutionStateProperty()
	: round(0), client_round(0), edit_distance(0), 
    symbolic_vars(0), symbolic_model(false), 
    recompute(true), is_recv_processing(false),
    inst_count(0), pass_count(0) {}

void ExecutionStateProperty::clone_helper(ExecutionStateProperty* p) { 
  p->round = round;
  p->client_round = client_round;
  p->edit_distance = edit_distance;
  p->symbolic_vars = symbolic_vars;
  p->symbolic_model = symbolic_model;
  p->recompute = recompute;
  p->is_recv_processing = is_recv_processing;
  p->inst_count = inst_count;
  p->pass_count = pass_count;
}

ExecutionStateProperty* ExecutionStateProperty::clone() { 
  ExecutionStateProperty* esp = new ExecutionStateProperty();
  clone_helper(esp);
  esp->recompute = true;
  return esp;
}

void ExecutionStateProperty::reset() {
  symbolic_vars = 0;
  edit_distance = 0;
  inst_count = 0;
  symbolic_model = false;

  //if (is_recv_processing) {
  //  CVMESSAGE("Resetting is_recv_processing");
  //  is_recv_processing = false;
  //}
}

// Order by greatest round number, then smallest edit distance
int ExecutionStateProperty::compare(const ExecutionStateProperty *b) const {
	const ExecutionStateProperty *_b = static_cast<const ExecutionStateProperty*>(b);

	if (round != _b->round)
		return round - _b->round;

	if (client_round != _b->client_round)
		return client_round - _b->client_round;

	if (pass_count != _b->pass_count)
		return pass_count - _b->pass_count;
  
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
	if (is_recv_processing)
	  os << "[Recv]";
	if (symbolic_model)
	  os << "[Sym]";
  os << "[IC: " << inst_count << "]";
  os << "[PC: " << pass_count << "]";
}

ExecutionStateProperty& ExecutionStateProperty::operator=(const ExecutionStateProperty& esp) {
  round = esp.round;
  client_round = esp.client_round;
  edit_distance = esp.edit_distance;
  symbolic_vars = esp.symbolic_vars;
  recompute = esp.recompute;
  is_recv_processing = esp.is_recv_processing;
  inst_count = esp.inst_count;
  pass_count = esp.pass_count;
}

//////////////////////////////////////////////////////////////////////////////

EditDistanceExecutionStateProperty::EditDistanceExecutionStateProperty() {}

ExecutionStateProperty* EditDistanceExecutionStateProperty::clone() { 
  ExecutionStateProperty* esp = new EditDistanceExecutionStateProperty();
  clone_helper(esp);
  esp->recompute = true;
  return esp;
}

// Only compare edit distance
int EditDistanceExecutionStateProperty::compare(
    const ExecutionStateProperty *b) const {

	const EditDistanceExecutionStateProperty *_b 
      = static_cast<const EditDistanceExecutionStateProperty*>(b);

  // Prioritize state that is currently recv_processing
	if (_b->is_recv_processing != is_recv_processing) 
    return (char)_b->is_recv_processing - (char)is_recv_processing;

  //// Edit distance is irrelevant if both states are recv_processing
	//if (_b->is_recv_processing == is_recv_processing &&
  //    _b->is_recv_processing != true) {
  //  // Reversed for priority queue!
  //  if (_b->edit_distance != edit_distance) {
  //    return _b->edit_distance - edit_distance;
  //  }
  //}

  // Reversed for priority queue!
  if (_b->edit_distance != edit_distance)
    return _b->edit_distance - edit_distance;

  if (round != _b->round)
    return round - _b->round;

  if (client_round != _b->client_round)
    return client_round - _b->client_round;
  
  // Reversed for priority queue!
  if (_b->symbolic_vars != symbolic_vars)
    return _b->symbolic_vars - symbolic_vars;

  return 0;
}

////////////////////////////////////////////////////////////////////////////////

} // End cliver namespace

