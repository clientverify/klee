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
  PathTreeNode* parent() { return parent_; }

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
	~PathTree();
	void branch(bool direction, klee::Solver::Validity validity, 
			klee::KInstruction* inst, CVExecutionState *state);

	void add_branched_state(CVExecutionState* state, 
			CVExecutionState* branched_state);

	bool get_states(const Path* path, const PathRange &range,
			ExecutionStateSet& states, int &index);

  bool get_child_states(PathTreeNode* node, ExecutionStateSet &states);

	bool contains_state(CVExecutionState* state);

	void remove_state(CVExecutionState* state);

	ExecutionStateSet& states() { return states_; }

 private:
	PathTreeNode* lookup_node(CVExecutionState* state);
	void add_state_to_map(CVExecutionState* state, PathTreeNode* node);
	void remove_state_from_map(CVExecutionState* state);

	PathTreeNode *root_;
	StateNodeMap state_node_map_;
	ExecutionStateSet states_;

};

////////////////////////////////////////////////////////////////////////////////

//template <class T>
//class Tree {
//  typedef Node<T> node_t;
// public:
//  Tree();
//  node_t* root() { return root_; }
//
// private:
//  node_t* root_;
//};
//
//template <class T>
//class Node {
// public:
//  typedef Node<T> node_t;
//
//  Node();
//  Node(node_t*);
//  Node(node_t*, const T&);
//
//  void add_child(node_t* node);
//  void insert_child_after(node_t* prev_node, node_t* node);
//  void insert_child_before(node_t* next_node, node_t* node);
//
//  node_t* parent()            { return parent_; }
//  node_t* first_child()       { return first_child_; }
//  node_t* last_child()        { return last_child_; }
//  node_t* next_sibling()      { return next_sibling_; }
//  node_t* previous_sibling()  { return previous_sibling_; }
//
//  void set_parent();
//
// private:
//  node_t* parent_;
//  node_t* first_child_, last_child_;
//  node_t* next_sibling_, previous_sibling_;
//
//  T info_;
//};
//
//template <class T>
//Node<T>::Node(node_t* parent, const T& info)
//  : parent_(parent), 
//    first_child_(0), 
//    last_child_(0), 
//    first_sibling_(0), 
//    prev_sibling_(0),
//    info_(info) {}

////////////////////////////////////////////////////////////////////////////////

class PathTreeFactory {
 public:
  static PathTree* create(CVExecutionState *root_state);
};

} // end namespace cliver

#endif // CLIVER_PATH_TREE_H

