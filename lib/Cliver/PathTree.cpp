//===-- PathTree.cpp -====---------------------------------------*- C++ -*-===//
//
// <insert license>
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//

#include "PathTree.h"
#include "CVCommon.h"
#include "CVExecutionState.h"
#include "klee/Internal/Module/KInstruction.h"

namespace cliver {

////////////////////////////////////////////////////////////////////////////////

PathTree::PathTree() {}

void PathTree::branch(bool direction, klee::Solver::Validity validity, 
			klee::KInstruction* inst, CVExecutionState *state) {}

////////////////////////////////////////////////////////////////////////////////

PathTreeNode::PathTreeNode(PathTreeNode* parent, klee::KInstruction* instruction)
	: parent_(parent),
	  true_node_(NULL),
	  false_node_(NULL),
		instruction_(instruction),
		is_fully_explored_(false) {}

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
