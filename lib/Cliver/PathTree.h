//===-- PathTree.h ----------------------------------------------*- C++ -*-===//
//
// <insert license>
//
//===----------------------------------------------------------------------===//
// TODO Rename this class to StateTree?
//
//===----------------------------------------------------------------------===//
#ifndef CLIVER_PATH_TREE_H
#define CLIVER_PATH_TREE_H

#include "klee/Solver.h"
#include "ExecutionStateProperty.h"
#include <set>
#include <map>

namespace llvm {
}

namespace klee {
	class KInstruction;
}

namespace cliver {

class CVExecutionState;

////////////////////////////////////////////////////////////////////////////////

class PathTreeNode;

class PathTree {
 typedef std::map< CVExecutionState*, PathTreeNode* > StateNodeMap;
 public:
	PathTree();
	void branch(bool direction, klee::Solver::Validity validity, 
			klee::KInstruction* inst, CVExecutionState *state);

	int get_states(const Path* path, const PathRange &range,
			ExecutionStateSet& states);

 private:
	PathTreeNode *root_;
	StateNodeMap state_node_map_;

};

class PathTreeNode {
 public:
	PathTreeNode(PathTreeNode* parent, klee::KInstruction* instruction);

	PathTreeNode* move_state_to_branch(bool direction, CVExecutionState* state, 
			klee::KInstruction* instruction);

	PathTreeNode* true_node();
	PathTreeNode* false_node();
	bool is_fully_explored(); 
	void add_state(CVExecutionState* state);
	ExecutionStateSet& states() { return states_; }
	klee::KInstruction* instruction() { return instruction_; } 

 private:
	PathTreeNode *parent_;
	PathTreeNode *true_node_;
	PathTreeNode *false_node_;
	klee::KInstruction* instruction_;
	ExecutionStateSet states_;
	bool is_fully_explored_; // True when all states node have reached a new node

};

////////////////////////////////////////////////////////////////////////////////

class PathTreeFactory {
 public:
  static PathTree* create();
};


} // end namespace cliver

#endif // CLIVER_PATH_TREE_H

