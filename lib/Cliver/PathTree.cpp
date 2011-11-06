//===-- PathTree.cpp -====---------------------------------------*- C++ -*-===//
//
// <insert license>
//
//===----------------------------------------------------------------------===//
// XXX Handle terminated states!
//
//===----------------------------------------------------------------------===//

#include "CVCommon.h"
#include "Path.h"
#include "PathTree.h"
#include "CVCommon.h"
#include "CVExecutionState.h"
#include "klee/Internal/Module/KInstruction.h"

namespace cliver {

////////////////////////////////////////////////////////////////////////////////

PathTree::PathTree(CVExecutionState* root_state) {
	root_ = new PathTreeNode(NULL, root_state->prevPC);
	root_->add_state(root_state);
 	state_node_map_[root_state] = root_;
}

PathTree::~PathTree() {
	CVDEBUG("PathTree deleted");
}

void PathTree::add_branched_state(CVExecutionState* state,
		CVExecutionState* branched_state) {
	
	PathTreeNode *node = lookup_node(state);
	assert(node && "Node lookup failed");

	node->add_state(branched_state);
 	state_node_map_[branched_state] = node;
}

/// TODO rename, too vague
bool PathTree::get_states(const Path* path, const PathRange &range,
		ExecutionStateSet& states, int &index) {

	PathTreeNode* node = root_;
	unsigned i = 0;

	while (node != NULL && node->is_fully_explored()) {
		if (true == path->get_branch(i)) {
			/// XXX fixme true_node may be null
			node = node->true_node();
		} else {
			node = node->false_node();
		}
		i++;
	}

	if (node == NULL || node->states().empty()) {
		index = -1;
		return false;
	}

	index = i;
	foreach (CVExecutionState* s, node->states()) {
		states.insert(s);
	}
	return true;
}

void PathTree::branch(bool direction, klee::Solver::Validity validity, 
			klee::KInstruction* inst, CVExecutionState *state) {
	
	// Lookup state in state_map_
	PathTreeNode *node = lookup_node(state);
	assert(node && "State not found during branch");
	
	// Move state from current node to child node; direction (false/true)
	//   Node may need to be created
	PathTreeNode *branch_node 
		= node->move_state_to_branch(direction, state, inst);

	// Update state_map_ with new (state,node) pair
 	state_node_map_[state] = branch_node;
}

bool PathTree::contains_state(CVExecutionState* state) {
	if (lookup_node(state) != NULL) {
		return true;
	}
	return false;
}

void PathTree::remove_state(CVExecutionState* state) {
	// Lookup state in state_map_
	PathTreeNode *node = lookup_node(state);
	assert(node && "State not found during remove");

	node->remove_state(state);

 	state_node_map_.erase(state);
}

PathTreeNode* PathTree::lookup_node(CVExecutionState* state) {
	PathTreeNode* node = NULL;

	StateNodeMap::iterator it = state_node_map_.find(state);
	if (it != state_node_map_.end())
		node = it->second;

	return node;
}

////////////////////////////////////////////////////////////////////////////////

PathTreeNode::PathTreeNode(PathTreeNode* parent, klee::KInstruction* instruction)
	: parent_(parent),
	  true_node_(NULL),
	  false_node_(NULL),
		instruction_(instruction),
		is_fully_explored_(false) {}

PathTreeNode* PathTreeNode::move_state_to_branch(bool direction, 
		CVExecutionState* state, klee::KInstruction* instruction) {

	assert(states_.count(state) && "State not at this node!");

	states_.erase(state);

	PathTreeNode* branch_node = NULL;

	if (true == direction) {
		if (!true_node_) {
			true_node_ = new PathTreeNode(this, instruction);
		}
		branch_node = true_node_;
	} else {
		if (!false_node_) {
			false_node_ = new PathTreeNode(this, instruction);
		}
		branch_node = false_node_;
	}

	assert(branch_node->instruction() == instruction);
	branch_node->add_state(state);

	update_explored_status();

	assert(branch_node);
	return branch_node;
}

void PathTreeNode::add_state(CVExecutionState* state) {
	assert(!is_fully_explored_ && "Can't add state, node is fully explored!");
	states_.insert(state);
}

void PathTreeNode::remove_state(CVExecutionState* state) {
	assert(states_.count(state) != 0 && "State not found during remove");
	states_.erase(state);
	update_explored_status();
}

PathTreeNode* PathTreeNode::true_node() {
	//assert(true_node_);
	return true_node_;
}

PathTreeNode* PathTreeNode::false_node() {
	//assert(false_node_);
	return false_node_;
}

bool PathTreeNode::is_fully_explored() {
	assert(is_fully_explored_ == states_.empty());
	return is_fully_explored_;
}

void PathTreeNode::update_explored_status() {
	if (!parent_) { // root node
		if (states_.empty()) {
			is_fully_explored_ = true;
		}
	} else if (states_.empty() && parent_->is_fully_explored()) {
		is_fully_explored_  = true;
	}
}

////////////////////////////////////////////////////////////////////////////////

PathTree* PathTreeFactory::create(CVExecutionState* root_state) {
  switch (g_cliver_mode) {
		case DefaultTrainingMode:
		case VerifyWithTrainingPaths:
		case DefaultMode:
		default:
			break;
  }
  return new PathTree(root_state);
}


} // end namespace cliver
