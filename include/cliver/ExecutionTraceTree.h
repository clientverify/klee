//===-- ExecutionTraceTree.h ------------------------------------*- C++ -*-===//
//
// <insert license>
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//
#ifndef CLIVER_EXECUTION_TRACE_TREE_H
#define CLIVER_EXECUTION_TRACE_TREE_H

#include "cliver/RadixTree.h"

////////////////////////////////////////////////////////////////////////////////

namespace cliver {

class CVExecutionState;

typedef uint16_t BasicBlockID;
typedef std::vector<BasicBlockID> ExecutionTrace;

class ExecutionTraceTree 
: public RadixTree<ExecutionTrace, BasicBlockID> {

  typedef RadixTree<ExecutionTrace, BasicBlockID> This;
  typedef This::Node Node;
  typedef This::Edge Edge;
  typedef This::EdgeMapIterator EdgeMapIterator;

  typedef std::map<CVExecutionState*, Node*> StateNodeMap;
  
#define foreach_edge(__node, __iterator) \
  EdgeMapIterator __iterator = __node->begin(); \
  EdgeMapIterator __iterator##_END = __node->end(); \
  for (; __iterator != __iterator##_END; ++__iterator)

 public:
  /// Default Constructor
  ExecutionTraceTree() { 
    this->root_ = new Node();
  }

  void trace_extend(ExecutionTrace &suffix, CVExecutionState* state = NULL) {
    if (state_node_map_.count(state)) {
      state_node_map_[state] = this->extend(suffix, state_node_map_[state]);
    } else {
      state_node_map_[state] = this->insert(suffix);
    }
  }

  void trace_extend(BasicBlockID &id, CVExecutionState* state = NULL) {
    if (state_node_map_.count(state)) {
      state_node_map_[state] = this->extend(id, state_node_map_[state]);
    } else {
      state_node_map_[state] = this->insert(id);
    }
  }

  /// Return a deep-copy of this RadixTree
  virtual This* clone() {

    // ExecutionTraceTree
    ExecutionTraceTree *tree = new ExecutionTraceTree();

    // Worklist holds a list of Node pairs, clone and original respectively
    std::stack<std::pair<Node*, Node*> > worklist; 
    worklist.push(std::make_pair(tree->root_, this->root_));

    // Node map used to copy state_node_map_
    std::map<Node*, Node*> nodemap;
    nodemap[this->root_] = tree->root_;

    while (!worklist.empty()) {
      Node* dst_node = worklist.top().first;
      Node* src_node = worklist.top().second;
      EdgeMapIterator 
          it = src_node->edge_map().begin(), iend = src_node->edge_map().end();

      worklist.pop();
      for (; it != iend; ++it) {
        Edge* src_edge = it->second;

        // Child clone node
        Node* dst_to_node = new Node();

        // Clone edge node
        Edge* edge = new Edge(dst_node, dst_to_node, 
                                        src_edge->begin(), src_edge->end());
        // Set 'parent_edge' (previously null)
        dst_to_node->set_parent_edge(edge);

        // Assign the edge to its key in the new node's edge map
        dst_node->edge_map()[edge->key()] = edge;

        // Add new node pair to worklist
        worklist.push(std::make_pair(dst_to_node, (Node*)src_edge->to()));

        // Add the node pair to the node map
        nodemap[(Node*)src_edge->to()] = dst_to_node;
      }
    }

    // Create state_node_map_for clone using same states but cloneed Node ptrs
    StateNodeMap::iterator it=state_node_map_.begin(), iend=state_node_map_.end();
    for (; it != iend; ++it) {
      assert(0 != nodemap.count(it->second));
      tree->state_node_map_[it->first] = nodemap[it->second];
    }

    // Return deep-copy clone
    return tree;
  }

 private:
  StateNodeMap state_node_map_;
};



} // end namespace cliver

#endif // CLIVER_EXECUTION_TRACE_TREE_H
