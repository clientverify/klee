//===-- CVExecutionState.cpp ------------------------------------*- C++ -*-===//
//
// <insert license>
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//

#include "CVExecutionState.h"
#include "CVExecutor.h"
#include "CVCommon.h"
#include "ExecutionStateProperty.h"
#include "NetworkManager.h"
#include "PathManager.h"
#include "PathTree.h"

#include "../Core/Common.h"
#include "llvm/Support/raw_ostream.h"

namespace cliver {

int CVExecutionState::next_id_ = 0;

CVExecutionState::CVExecutionState(klee::KFunction *kF, klee::MemoryManager *mem)
 : klee::ExecutionState(kF, mem), 
	 id_(increment_id()) {}

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

void CVExecutionState::initialize(CVExecutor *executor) {
  id_ = increment_id();
  coveredNew = false;
  coveredLines.clear();
	network_manager_ = NetworkManagerFactory::create(this);
	path_manager_ = PathManagerFactory::create();
	property_ = ExecutionStatePropertyFactory::create();
	path_tree_ = PathTreeFactory::create(this);
}

CVExecutionState* CVExecutionState::clone() {
  CVExecutionState *cloned_state = new CVExecutionState(*this);
  cloned_state->id_ = increment_id();
  cloned_state->network_manager_ 
		= network_manager_->clone(cloned_state); 
	cloned_state->path_manager_ = path_manager_->clone();
  cloned_state->property_ = property_->clone();
	cloned_state->path_tree_ = path_tree_;

  return cloned_state;
}

CVExecutionState* CVExecutionState::branch() {
  depth++;
  CVExecutionState *branched_state = clone();
	path_tree_->add_branched_state(this, branched_state);
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

void CVExecutionState::reset_path_tree(PathTree* path_tree) {
	if (path_tree)
		path_tree_ = path_tree;
	else
		path_tree_ = PathTreeFactory::create(this);
}

bool CVExecutionStateLT::operator()(const CVExecutionState* a, 
		const CVExecutionState* b) const {
	return a->compare(*b) < 0;
}

} // End cliver namespace

