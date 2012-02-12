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
#include "CVCommon.h"
#include "cliver/ExecutionStateProperty.h"
#include "cliver/NetworkManager.h"
#include "cliver/PathManager.h"

#include "../Core/Common.h"
#include "klee/Internal/Module/InstructionInfoTable.h"
#include "llvm/Support/raw_ostream.h"

namespace cliver {

int CVExecutionState::next_id_ = 0;

CVExecutionState::CVExecutionState(klee::KFunction *kF, klee::MemoryManager *mem)
 : klee::ExecutionState(kF, mem),
	 id_(increment_id()), property_(0) {}

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
	path_manager_ = PathManagerFactory::create();
	property_ = ExecutionStatePropertyFactory::create();
#ifdef DEBUG_CLIVER_STATE_LOG
	debug_log_ = new std::stringstream();
#endif
}

CVExecutionState* CVExecutionState::clone() {
  CVExecutionState *cloned_state = new CVExecutionState(*this);
  cloned_state->id_ = increment_id();
  cloned_state->network_manager_ 
		= network_manager_->clone(cloned_state); 
	cloned_state->path_manager_ = path_manager_->clone();
  cloned_state->property_ = property_->clone();
#ifdef DEBUG_CLIVER_STATE_LOG
	cloned_state->debug_log_ = new std::stringstream();
	*(cloned_state->debug_log_) << debug_log_->str();
#endif
  cloned_state->cv_ = cv_;
  cv_->notify_all(ExecutionEvent(CV_STATE_CLONE, cloned_state, this));
  return cloned_state;
}

CVExecutionState* CVExecutionState::branch() {
  depth++;
  CVExecutionState *branched_state = clone();
	// Alert path manager of state branch event
	path_manager_->state_branch(this, branched_state);
  branched_state->coveredNew = false;
  branched_state->coveredLines.clear();
  weight *= .5;
  branched_state->weight -= weight;
  return branched_state;
}

void CVExecutionState::reset_path_manager(PathManager* path_manager) {
	if (path_manager_) 
		delete path_manager_;

	if (path_manager)
		path_manager_ = path_manager;
	else
		path_manager_ = PathManagerFactory::create();
}

void CVExecutionState::print(std::ostream &os) const {
  os << "[" << this << "] [id:" << id_ << "] " << *property_;
}

std::ostream &operator<<(std::ostream &os, const CVExecutionState &s) {
  s.print(os);
  return os;
}

} // End cliver namespace

