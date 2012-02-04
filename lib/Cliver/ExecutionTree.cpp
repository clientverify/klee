//===-- ExecutionTree.cpp -====----------------------------------*- C++ -*-===//
//
// <insert license>
//
//===----------------------------------------------------------------------===//
// TODO: Method to merge ExecutionTrees
// TODO: Method to modify pre-existing ExecutionTree
// TODO: Method to split an ExecutionTree given a list of Leaf nodes
// TODO: Optimization: store BasicBlock entry id's in a vector rather than a 
//       path of nodes
// TODO: Unit tests for execution trees
// TODO: Remove static_casts in notify()
//===----------------------------------------------------------------------===//

#include "CVCommon.h"
#include "CVExecutor.h"
#include "ExecutionTree.h"
#include "CVExecutionState.h"
#include "klee/Internal/Module/KInstruction.h"
#include "klee/Internal/Module/InstructionInfoTable.h"
#include "llvm/Support/raw_ostream.h"

#include <fstream>

namespace cliver {

llvm::cl::opt<bool>
DebugExecutionTree("debug-execution-tree",llvm::cl::init(false));

#ifndef NDEBUG

#undef CVDEBUG
#define CVDEBUG(x) \
	__CVDEBUG(DebugExecutionTree, x);

#undef CVDEBUG_S
#define CVDEBUG_S(__state_id, __x) \
	__CVDEBUG_S(DebugExecutionTree, __state_id, __x)

#else

#undef CVDEBUG
#define CVDEBUG(x)

#undef CVDEBUG_S
#define CVDEBUG_S(__state_id, __x)

#endif

////////////////////////////////////////////////////////////////////////////////

// Helper for debug output
inline std::ostream &operator<<(std::ostream &os, 
		const klee::KInstruction &ki) {
	std::string str;
	llvm::raw_string_ostream ros(str);
	ros << ki.info->id << ":" << *ki.inst;
	//str.erase(std::remove(str.begin(), str.end(), '\n'), str.end());
	return os << ros.str();
}

////////////////////////////////////////////////////////////////////////////////

BasicBlockEntryInfo::BasicBlockEntryInfo(CVExecutionState* state) {
  states.insert(state);
  basic_block_entry_id = state->prevPC->info->id;
  basic_block_ = state->prevPC->inst->getParent();
  pending_count = 0;
}

bool BasicBlockEntryInfo::operator==(const BasicBlockEntryInfo& b) const {
  return basic_block_entry_id == b.basic_block_entry_id;
}
bool BasicBlockEntryInfo::operator!=(const BasicBlockEntryInfo& b) const {
  return basic_block_entry_id != b.basic_block_entry_id;
}

////////////////////////////////////////////////////////////////////////////////

ExecutionTree::ExecutionTree() {
  tree_.set_root(new BasicBlockEntryInfo());
}

ExecutionTree::~ExecutionTree() {
  CVDEBUG("Writing ExecutionTree " << tree_.size() );
  //std::ofstream ofs("/home/rac/test.tree");
  //boost::archive::text_oarchive oa(ofs);
  //oa << tree_;
  //ofs.close();
}

void ExecutionTree::notify(ExecutionEvent ev) {
  switch (ev.event_type) {
    case CV_BASICBLOCK_ENTRY: {
      //CVDEBUG("Adding node to ExecutionTree " << tree_.size() );
      
      CVExecutionState* state = static_cast<CVExecutionState*>(ev.state);
      // if state in map: 
      //    get node, walk children, assert bb_id != child
      //    remove state from current node
      //    add_child(basic_block_id), add state
      //    update state_map
      // if state not in map: 
      //    get state->parent, get parent node 
      //       if state->parent == NULL, get root node
      //    walk children, assert bb_id != child
      //    add_child(basic_block_id), add state
      //    update state_map

      node_iterator node;
      if (state_map_.empty()) {
        CVDEBUG("Adding state at root: " << state->id() );
        node = tree_.root();

      } else if (state_map_.count(state)) {
        state_map_t::iterator it = state_map_.find(state);
        node = it->second;
        (*node)->states.erase(state);

      } else if (pending_cloned_states_.count(state)) {
        state_map_t::iterator it = pending_cloned_states_.find(state);
        node = it->second;
        pending_cloned_states_.erase(state);
        assert((*node)->pending_count > 0);
        (*node)->pending_count--;

      } else if (g_cliver_mode == DefaultTrainingMode ) {
        CVDEBUG("Adding training state at root: " << state->id() );
        node = tree_.root();
      } else {
        CVDEBUG("State not found in state_map: " << state->id());
        assert(0);
      }

      //CVDEBUG("Adding Node: " << *(state->prevPC));
      add_child_node(node, state);
      
      break;
    }

    case CV_STATE_REMOVED: {
      CVExecutionState* state = static_cast<CVExecutionState*>(ev.state);
      assert(state_map_.count(state) == 1);
      node_iterator node;
      if (pending_cloned_states_.count(state)) {
        CVDEBUG("Removing pending state: " << state->id() );
        state_map_t::iterator it = pending_cloned_states_.find(state);
        node = it->second;
        pending_cloned_states_.erase(state);
        assert((*node)->pending_count > 0);
        (*node)->pending_count--;
      } else if (state_map_.count(state)) {
        CVDEBUG("Removing state: " << state->id() );
        state_map_t::iterator it = state_map_.find(state);
        node = it->second;
        (*node)->states.erase(state);
        int count = 0;
        while ((*node)->states.empty() && (*node)->pending_count == 0 
               && node != tree_.root() && node.number_of_children() == 0) {
          // Prune up to first state without pending nodes
          //CVDEBUG("Pruning Node: "
          //  << *(g_executor->get_instruction((*node)->basic_block_entry_id)));
          count++;
          node_iterator removed_node(node.node);
          node = tree_.parent(node);

          delete *removed_node;
          tree_.erase(removed_node);
        }
        state_map_.erase(state);
        CVDEBUG("Pruned " << count << " nodes from tree");
      } else {
        CVDEBUG("Removed state not found on in state_map");
        assert(0);
      }
      break;
    }

    case CV_STATE_CLONE: {
      CVExecutionState* state = static_cast<CVExecutionState*>(ev.state);
      CVExecutionState* parent = static_cast<CVExecutionState*>(ev.parent);
      CVDEBUG("Cloned state: " << state->id() << ", parent " << parent->id() << ", "<< *(state->prevPC));
      assert(parent);
      node_iterator node;
      if (state_map_.count(parent)) {
        state_map_t::iterator it = state_map_.find(parent);
        node = it->second;
      } else if (pending_cloned_states_.count(parent)) {
        state_map_t::iterator it = pending_cloned_states_.find(parent);
        node = it->second;
      } else {
        CVDEBUG("Parent state not found on clone");
        assert(0);
      }
      pending_cloned_states_[state] = node;
      (*node)->pending_count++;
      break;
    }
  }
}

void ExecutionTree::add_child_node(node_iterator &node, 
                                   CVExecutionState* state) {
  // Create data for node
  BasicBlockEntryInfo *node_info = new BasicBlockEntryInfo(state);

  //Check if this node already exists
  foreach_child (tree_t, tree_, node, cit) {
    if (*node_info == *(*cit)) {
      // XXX This happens because node is added before branch is taken?
      delete node_info;
      node_info = *cit;
      node_info->states.insert(state);
      state_map_[state] = node_iterator(cit.node);
      if (node_info->states.size()) {
        CVDEBUG("Duplicate state: " << state->id() << " " << *(state->prevPC));
        //CVDEBUG("Duplicate States (" << node_info->states.size() << ") at Node: " 
        //        << *(g_executor->get_instruction(node_info->basic_block_entry_id)));
      }
      return;
    }
  }

  // Append new child node
  state_map_[state] = tree_.append_child(node, node_info);
}

void ExecutionTree::get_path_from_leaf(leaf_iterator &leaf,
                                       ExecutionPath &path) {
  foreach_parent(tree_t, tree_, leaf, it) {
    path.push_back(*it);
  }
  std::reverse(path.begin(), path.end());
}

void ExecutionTree::get_path_set(ExecutionPathSet &path_set) { 
  foreach_leaf (tree_t, tree_, tree_.root(), it) {
    ExecutionPath *path = new ExecutionPath();
    get_path_from_leaf(it, *path);
    path_set.insert(path);
  }
}

} // end namespace cliver
