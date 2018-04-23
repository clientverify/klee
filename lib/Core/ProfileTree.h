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
    
    std::pair<Node*,Node*> split(Node *n,
                                 const data_type &leftData,
                                 const data_type &rightData);

    int get_total_branch_count(void);
    void postorder(ProfileTreeNode* p, int indent=0);
  private:
    static int total_branch_count;

  };

  class ProfileTreeNode {
    friend class ProfileTree;
  public:
    ProfileTreeNode *parent, *left, *right;
    ExecutionState *data;
    ref<Expr> condition;
    void increment_ins_count(void);
    int get_ins_count(void);
    int get_total_ins_count(void);

  private:
    ProfileTreeNode(ProfileTreeNode *_parent, ExecutionState *_data);
    ~ProfileTreeNode();
    int ins_count;
    static int total_ins_count;
  };
}

