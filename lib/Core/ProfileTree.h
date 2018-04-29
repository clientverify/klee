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
namespace klee {
  class ExecutionState;

  class ProfileTree { 
    typedef ExecutionState* data_type;
    Mutex lock_;

  public:
    typedef class ProfileTreeNode Node;
    Node *root;

    ProfileTree(const data_type &_root);
    ~ProfileTree();
    
    int postorder(ProfileTreeNode* p, int indent=0);
  };

  class ProfileTreeNode {
    friend class ProfileTree;
  public:
    ProfileTreeNode *parent;
    std::vector<ProfileTreeNode*> children;
    ExecutionState *data;
    ref<Expr> condition;
    void function_call(
        ExecutionState* data,
        llvm::Instruction* ins);

    void function_return(
        ExecutionState* data,
        llvm::Instruction* ins);

    std::pair<ProfileTreeNode*, ProfileTreeNode*> branch(
                                 ExecutionState* leftData,
                                 ExecutionState* rightData,
                                 llvm::Instruction* ins);

    std::pair<ProfileTreeNode*, ProfileTreeNode*> clone(
                                 ExecutionState* me_state,
                                 ExecutionState* clone_state,
                                 llvm::Instruction* ins);
    void increment_ins_count(void);
    int get_ins_count(void);
    int get_total_ins_count(void);
    int get_total_node_count(void);
    int get_total_clone_count(void);
    int get_total_ret_count(void);
    int get_total_call_count(void);
    int get_total_branch_count(void);

    enum NodeType { leaf, clone_parent, branch_parent, function_parent, function_return_parent };
    enum NodeType get_type(void);
    llvm::Instruction* get_instruction(void);



  private:
    //leaf: this is the type when a node hasn't split yet.
    //clone_parent: this is the type when a node is split as a result of a clone
    //  call
    //branch_parent: this is the type when a node is split as a result of a branch
    NodeType my_type;
    ProfileTreeNode* link(
        ExecutionState* data,
        llvm::Instruction* ins);

    std::pair<ProfileTreeNode*, ProfileTreeNode*> split(
                                 ExecutionState* leftData,
                                 ExecutionState* rightData,
                                 llvm::Instruction* ins);

    ProfileTreeNode(ProfileTreeNode *_parent,
                    ExecutionState *_data,
                    llvm::Instruction* ins);
    ~ProfileTreeNode();
    //All the instructions executed by this node's execution state
    int ins_count;
    int my_node_number;
    //The instruction associated with this node's creation.  E.g. branch
    //node would have the branch instruction where this node's execution state
    //was created.  Should only be NULL for root.
    llvm::Instruction* my_instruction;

    //All the instructions in the tree
    static int total_ins_count;
    static int total_node_count;
    static int total_branch_count;
    static int total_clone_count;
    static int total_function_call_count;
    static int total_function_ret_count;
  };
}

