//===-- PathTree.cpp -====---------------------------------------*- C++ -*-===//
//
// <insert license>
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//

#include "Path.h"
#include "PathTree.h"
#include "CVCommon.h"
#include "CVExecutionState.h"
#include "klee/Internal/Module/KInstruction.h"

namespace cliver {

////////////////////////////////////////////////////////////////////////////////

PathTree::PathTree() {}

CVExecutionState* PathTree::get_state(const Path* path, const PathRange &range) {

	CVExecutionState* state = NULL;
	PathTreeNode* node = root_;
	unsigned index = 0;

	while (node->is_fully_explored()) {
		if (true == path->get_branch(index)) {
			node = node->true_node();
		} else {
			node = node->false_node();
		}
		index++;
	}
	
	return state;
}

void PathTree::branch(bool direction, klee::Solver::Validity validity, 
			klee::KInstruction* inst, CVExecutionState *state) {}

////////////////////////////////////////////////////////////////////////////////

PathTreeNode::PathTreeNode(PathTreeNode* parent, klee::KInstruction* instruction)
	: parent_(parent),
	  true_node_(NULL),
	  false_node_(NULL),
		instruction_(instruction),
		is_fully_explored_(false) {}

PathTreeNode* PathTreeNode::true_node() {
	assert(true_node_);
	return true_node_;
}

PathTreeNode* PathTreeNode::false_node() {
	assert(false_node_);
	return false_node_;
}

bool PathTreeNode::is_fully_explored() {
	assert(!is_fully_explored_ == states_.empty());
	return is_fully_explored_;
}

////////////////////////////////////////////////////////////////////////////////

PathTree* PathTreeFactory::create() {
  switch (g_cliver_mode) {
		case DefaultTrainingMode:
		case VerifyWithTrainingPaths:
		case DefaultMode:
		default:
			break;
  }
  return new PathTree();
}


} // end namespace cliver
