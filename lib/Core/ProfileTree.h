//===-- ProfileTree.h -------------------------------------------------*- C++ -*-===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//


#include <klee/Expr.h>
#include <klee/util/Mutex.h>
#include "llvm/IR/Instruction.h"
#include <vector>
namespace cliver {
      class SearcherStage;
      class ClientVerifier;
}

namespace klee {
  class ExecutionState;

  class ProfileTree {

  public:
    typedef class ProfileTreeNode Node;
    Node *root;

    ProfileTree(const ExecutionState* es);
    ~ProfileTree();

    //All the instructions in the tree
    int total_ins_count = 0;
    int total_node_count = 0;
    int total_branch_count = 0;
    int total_clone_count = 0;
    int total_function_call_count = 0;
    int total_function_ret_count = 0;

    int get_total_ins_count(void);
    int get_total_node_count(void);
    int get_total_clone_count(void);
    int get_total_ret_count(void);
    int get_total_call_count(void);
    int get_total_branch_count(void);

    void post_processing_dfs(ProfileTreeNode *root);
    void validate_correctness();
    void consolidate_function_data();
    //Prints the branch clone graph.  Does not print function calls/returns
    void dump_branch_clone_graph(std::string path, cliver::ClientVerifier* cv_);
  };


  //Each ProfileNode has a ContainerNode, initially NULL, which will contain the
  //instruction associated with the event the node encounters (clone, symbolic branch, function call,
  //or function return) which causes the node to stop being a leaf node.
  //Containers contain the instruction associated with this event and
  //subsequent metadata associated with the instruction.
  class ContainerNode{
  public:
    ContainerNode(llvm::Instruction* i);
    virtual ~ContainerNode() = default;
    //The instruction associated with this node's creation.  E.g. branch
    //node would have the branch instruction. Should only be NULL for root.
    llvm::Instruction* my_instruction;
  };

  //Stores additional metadata associated with call instructions.
  //ProfileTreeNodes from calls made in the target function are
  //added to my_calls.  This means you can traverse the call graph
  //of the tree.
  class ContainerCallIns: public ContainerNode{
  public:
    ContainerCallIns(llvm::Instruction* i, llvm::Function* target);
    virtual ~ContainerCallIns() = default;
    //function being called.
    llvm::Function* my_target;
    //stores call nodes generated by calls in this->my_target.
    std::vector<ProfileTreeNode*> my_calls;
    //counts the instructions executed in my_target (but not the functions it calls).
    int function_ins_count;
    //counts the instructions executed by my_target's subfunctions.
    int function_calls_ins_count;
    //counts the symbolic branches executed in this call to my_target.
    int function_branch_count;
    //counts the symbolic branches executed by functions (transitively)
    //called by my_target.
    int function_calls_branch_count;
  };

  class ContainerRetIns: public ContainerNode{
  public:
    ContainerRetIns(llvm::Instruction* i, llvm::Instruction* to);
    virtual ~ContainerRetIns() = default;
    //function being returned to.
    llvm::Instruction* my_return_to;
  };

  //Maintains pointers to ProfileTreeNode generated by the next symbolic
  //branches/clones.  This allows you to traverse the branching tree--ignoring
  //function calls/returns.
  class ContainerBranchClone: public ContainerNode{
  public:
    ContainerBranchClone(llvm::Instruction* i, cliver::SearcherStage *s);
    virtual ~ContainerBranchClone() = default;
    //branches/clones immedidiately following this in the graph.
    std::vector<ProfileTreeNode*> my_branches_or_clones;
  };


  class FunctionStatstics{
    public:
      FunctionStatstics(ContainerCallIns* c);
      ~FunctionStatstics();
      int ins_count;
      //The number of instructions executed between calls to this function and
      //their respective returns.
      int sub_functions_ins_count;
      //Symbolic branches executed in this function.
      int branch_count;
      //Symbolic branches executed in functions this function calls (transitively).
      int sub_functions_branch_count;
      int times_called;
      llvm::Function* function;
      void add(ContainerCallIns* c);
  };

  /* Each ExecutionState has a ProfileTreeNode, it gets a new profile node when
   * it encounters a function call, return, symbolic branch, or clone.
   *
   * A node is initially a root or leaf node.  There is only one root node
   * and it remains a root node throughout.  All other nodes begin as leaf
   * nodes and transition into one of the following node types if an
   * associated event is encountered in their execution:
   *    clone_parent  - transition result of clone of ExecutionState that isn't
   *                    a symbolic branch
   *    branch_parent - transition result of clone of ExecutionState due to
   *                    symbolic branch.
   *    call_parent   - assigned when execution encounters a call instruction.
   *    return_parent - assigned when execution encounters a return instruction.
   * When a node transitions, it stops recording instruction statistics,
   * and is assigned a container which records metadata related to execution after
   * the transition.
  */
  class ProfileTreeNode {
    friend class ProfileTree;
    friend class FunctionStatstics;
  public:
    ProfileTreeNode *parent;
    std::vector<ProfileTreeNode*> children;
    ContainerNode* container;
    llvm::Instruction* last_instruction;
    cliver::SearcherStage *stage;

    //function_call, function_return, branch and clone update the appropriate
    //execution states' ProfileTree node, and may change the current node from a
    //leaf node to be the appropriate NodeType.
    void record_function_call(
        ExecutionState* es,
        llvm::Instruction* ins,
        llvm::Function* target);

    void record_function_return(
        ExecutionState* es,
        llvm::Instruction* ins,
        llvm::Instruction* to);

    void record_symbolic_branch(
        ExecutionState* leftEs,
        ExecutionState* rightEs,
        llvm::Instruction* ins);

    void record_clone(
        ExecutionState* me_state,
        ExecutionState* clone_state,
        llvm::Instruction* ins,
        cliver::SearcherStage* stage);
    void increment_ins_count(llvm::Instruction *i);
    void increment_branch_count(void);
    int get_ins_count(void);

    int get_depth();
    void update_function_statistics(void);
    void report_function_data(std::unordered_map<std::string, FunctionStatstics*>* stats);

    enum NodeType { leaf, clone_parent, branch_parent, call_parent, root,
      return_parent };
    enum NodeType get_type(void);
    llvm::Instruction* get_instruction(void);

  private:
    ProfileTree* my_tree;
    NodeType my_type;

    //Creates a single child node receiving the parent's data.  Used on function call and return.
    ProfileTreeNode* link(ExecutionState* es);

    //Creates two children nodes for this, assigns them to the execution
    //states.  Called when handling a symbolic branch or clone.
    std::pair<ProfileTreeNode*, ProfileTreeNode*> split(
                                 ExecutionState* leftEs,
                                 ExecutionState* rightEs);

    ProfileTreeNode(const ExecutionState* es, ProfileTree* tree);

    ProfileTreeNode(ProfileTreeNode *_parent,
                    const ExecutionState* es);

    ~ProfileTreeNode();

    //All the instructions executed by this node's execution state (only
    //incremented while a leaf node).
    int ins_count;

    //Used by most nodes.  Should be a function node, or root node indicating
    //the function executing in.
    ProfileTreeNode* my_function;

    //points to the last branch or clone node
    ProfileTreeNode* my_branch_or_clone;

    //the number of instructions executed along the path from the root to this
    //node.  Incremented while a leaf node.
    int depth;
  };

}

