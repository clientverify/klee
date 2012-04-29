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

#include "llvm/Support/raw_ostream.h"

namespace cliver {

int CVExecutionState::next_id_ = 0;

CVExecutionState::CVExecutionState(klee::KFunction *kF, klee::MemoryManager *mem)
 : klee::ExecutionState(kF, mem),
	 id_(increment_id()), property_(0), basic_block_tracking_(true) {}

CVExecutionState::CVExecutionState(
    const std::vector< klee::ref<klee::Expr> > &assumptions)
    : klee::ExecutionState(assumptions) {
  cv_error("Not supported.");
}

CVExecutionState::~CVExecutionState() {
  while (!stack.empty()) 
    popFrame();

  if (network_manager_)
    delete network_manager_;

  if (property_)
    delete property_;
}

int CVExecutionState::compare(const CVExecutionState& b) const {
	return property_->compare(*b.property_);
}

void CVExecutionState::get_pc_string(std::string &rstr,
		llvm::Instruction* inst) {
	llvm::raw_string_ostream ros(rstr);
	if (inst)
		ros << *(inst);
	else
		ros << *(pc->inst);
	rstr.erase(std::remove(rstr.begin(), rstr.end(), '\n'), rstr.end());
	ros.flush();
}

void CVExecutionState::initialize(ClientVerifier *cv) {
  cv_ = cv;
  id_ = increment_id();
  coveredNew = false;
  coveredLines.clear();
	network_manager_ = NetworkManagerFactory::create(this,cv);
	property_ = ExecutionStatePropertyFactory::create();
}

CVExecutionState* CVExecutionState::clone(ExecutionStateProperty* property) {
  assert(cv_->executor()->replay_path() == NULL);
  CVExecutionState *cloned_state = new CVExecutionState(*this);
  cloned_state->id_ = increment_id();
  cloned_state->network_manager_ 
		= network_manager_->clone(cloned_state); 

  if (property != NULL)
    cloned_state->property_ = property;
  else
    cloned_state->property_ = property_->clone();

  cloned_state->cv_ = cv_;
  cloned_state->basic_block_tracking_ = basic_block_tracking_;

  if (property == NULL)
    cv_->notify_all(ExecutionEvent(CV_STATE_CLONE, cloned_state, this));

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

void CVExecutionState::print(std::ostream &os) const {
  if (property_)
    os << "[" << this << "] [id:" << id_ << "] " << "[" << property_ << "] " << *property_;
  else 
    os << "[" << this << "] [id:" << id_ << "] ";
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

