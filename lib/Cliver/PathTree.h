//===-- PathTree.h ----------------------------------------------*- C++ -*-===//
//
// <insert license>
//
//===----------------------------------------------------------------------===//
// TODO Rename this class to StateTree?
//
// Handle paths that reach null node, which is complete
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

class PathTreeNode {
 public:
	PathTreeNode(PathTreeNode* parent, klee::KInstruction* instruction);

	PathTreeNode* move_state_to_branch(bool direction, CVExecutionState* state, 
			klee::KInstruction* instruction);

	PathTreeNode* true_node();
	PathTreeNode* false_node();
	bool is_fully_explored(); 
	void add_state(CVExecutionState* state);
	void remove_state(CVExecutionState* state);
	ExecutionStateSet& states() { return states_; }
	klee::KInstruction* instruction() { return instruction_; } 

 private:
	void update_explored_status();

	PathTreeNode *parent_;
	PathTreeNode *true_node_;
	PathTreeNode *false_node_;
	klee::KInstruction* instruction_;
	ExecutionStateSet states_;
	bool is_fully_explored_; // True when all states node have reached a new node

};

class PathTree {
 typedef std::map< CVExecutionState*, PathTreeNode* > StateNodeMap;
 public:
	PathTree(CVExecutionState* root_state);
	void branch(bool direction, klee::Solver::Validity validity, 
			klee::KInstruction* inst, CVExecutionState *state);

	void add_branched_state(CVExecutionState* state, 
			CVExecutionState* branched_state);

	int get_states(const Path* path, const PathRange &range,
			ExecutionStateSet& states);

	bool get_states(const Path* path, const PathRange &range,
			ExecutionStateSet& states, int &index);

	bool contains_state(CVExecutionState* state);

	void remove_state(CVExecutionState* state);

 private:
	PathTreeNode* lookup_node(CVExecutionState* state);


	PathTreeNode *root_;
	StateNodeMap state_node_map_;

};

////////////////////////////////////////////////////////////////////////////////

class PathTreeFactory {
 public:
  static PathTree* create(CVExecutionState *root_state);
};


} // end namespace cliver

#endif // CLIVER_PATH_TREE_H

