//===-- CVExecutionState.cpp ------------------------------------*- C++ -*-===//
//
// <insert license>
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//

#include "cliver/CVExecutionState.h"
#include "cliver/CVExecutor.h"
#include "cliver/CVStream.h"
#include "cliver/ExecutionStateProperty.h"
#include "cliver/NetworkManager.h"
#include "CVCommon.h"

#include "../Core/Common.h"
#include "klee/Internal/Module/InstructionInfoTable.h"
#include "klee/Internal/Module/KInstruction.h"
#include "klee/util/RefCount.h"

#include "llvm/ADT/StringExtras.h"

namespace cliver {

klee::RefCount CVExecutionState::next_id_ = 0;

CVExecutionState::CVExecutionState(klee::KFunction *kF)
 : klee::ExecutionState(kF),
	 id_(increment_id()), property_(0), basic_block_tracking_(true),
   multi_pass_clone_(NULL) {}

CVExecutionState::CVExecutionState(
    const std::vector< klee::ref<klee::Expr> > &assumptions)
    : klee::ExecutionState(assumptions) {
  cv_error("Not supported.");
}

CVExecutionState::~CVExecutionState() {
  stats::state_remove_count += 1;
  while (!stack.empty()) 
    popFrame();

  if (network_manager_)
    delete network_manager_;

  if (property_)
    delete property_;
}

int CVExecutionState::compare(const CVExecutionState& b) const {
	return property_->compare(b.property_);
}

void CVExecutionState::initialize(ClientVerifier *cv) {
  cv_ = cv;
  id_ = increment_id();
  event_flag_ = false;
  coveredNew = false;
  coveredLines.clear();
	network_manager_ = NetworkManagerFactory::create(this,cv);
	property_ = ExecutionStatePropertyFactory::create();
  multi_pass_clone_ = NULL;
}

CVExecutionState* CVExecutionState::clone(ExecutionStateProperty* property) {
  stats::state_clone_count += 1;
  assert(cv_->executor()->replay_path() == NULL);
  CVExecutionState *cloned_state = new CVExecutionState(*this);
  cloned_state->id_ = increment_id();
  cloned_state->event_flag_ = event_flag_;
  cloned_state->network_manager_ 
		= network_manager_->clone(cloned_state); 

  if (property != NULL) {
    cloned_state->property_ = property;
    cloned_state->multi_pass_clone_ = NULL;
  } else {
    cloned_state->property_ = property_->clone();
    cloned_state->multi_pass_clone_ = multi_pass_clone_;
  }

  cloned_state->cv_ = cv_;
  cloned_state->basic_block_tracking_ = basic_block_tracking_;

  if (property == NULL)
    cv_->notify_all(ExecutionEvent(CV_STATE_CLONE, cloned_state, this));

  cloned_state->array_name_index_map_ = array_name_index_map_;

  cloned_state->multi_pass_assignment_ = multi_pass_assignment_;

  return cloned_state;
}

CVExecutionState* CVExecutionState::branch() {
  depth++;
  CVExecutionState *branched_state = clone();
  branched_state->coveredNew = false;
  branched_state->coveredLines.clear();
  weight *= .5;
  branched_state->weight -= weight;
  return branched_state;
}

void CVExecutionState::erase_self() {
  set_property(NULL);
  cv_->executor()->remove_state_internal_without_notify(this);
}

void CVExecutionState::erase_self_permanent() {
  cv_->executor()->remove_state_internal(this);
}

unsigned CVExecutionState::get_current_basic_block() {
  return this->prevPC->kbb->id;
}

std::string CVExecutionState::get_unique_array_name(const std::string &s) {
  // Look up unique name for this variable, incremented per variable name
  return s + "_" + llvm::utostr(array_name_index_map_[s]++);
}

void CVExecutionState::print(std::ostream &os) const {
  // Print state and property info
  if (property_ != NULL)
    os << "[" << this << "][id:" << id_ << "] " << *property_;
  else
    os << "[" << this << "][id:" << id_ << "] ";

  // Print current basic block id and instruction
  os << "[BB:" << this->prevPC->kbb->id << "]" << " " << *pc;
}

std::ostream &operator<<(std::ostream &os, const CVExecutionState &s) {
  s.print(os);
  return os;
}

bool CVExecutionStateLT::operator()(const CVExecutionState* a, 
		const CVExecutionState* b) const {
	return a->compare(*b) < 0;
}

} // End cliver namespace

