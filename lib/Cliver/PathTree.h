//===-- PathTree.h ----------------------------------------------*- C++ -*-===//
//
// <insert license>
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//
#ifndef CLIVER_PATH_TREE_H
#define CLIVER_PATH_TREE_H

#include "klee/Solver.h"
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
 public:
	PathTree();
	void branch(bool direction, klee::Solver::Validity validity, 
			klee::KInstruction* inst, CVExecutionState *state);

 private:
	PathTreeNode *root_;
	std::map< CVExecutionState*, PathTreeNode* > state_map_;

};

class PathTreeNode {
 public:
	PathTreeNode(PathTreeNode* parent, klee::KInstruction* instruction);

 private:
	PathTreeNode *parent_;
	PathTreeNode *true_node_;
	PathTreeNode *false_node_;
	klee::KInstruction* instruction_;
	std::set<CVExecutionState*> states_;
	bool is_fully_explored_; // True when all states node have reached a new node

};

////////////////////////////////////////////////////////////////////////////////

class PathTreeFactory {
 public:
  static PathTree* create();
};


} // end namespace cliver

#endif // CLIVER_PATH_TREE_H

