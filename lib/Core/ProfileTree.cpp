//===-- ProfileTree.cpp ---------------------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "ProfileTree.h"

#include <klee/Expr.h>
#include <klee/util/ExprPPrinter.h>

#include "llvm/Support/CommandLine.h"
#include <iostream>
#include <iomanip>

#include <vector>

using namespace klee;
int ProfileTreeNode::total_ins_count = 0;
int ProfileTree::total_branch_count = 0;

ProfileTree::ProfileTree(const data_type &_root) : root(new Node(0,_root)) {
}

ProfileTree::~ProfileTree() {}


std::pair<ProfileTreeNode*, ProfileTreeNode*>
ProfileTree::split(Node *n, 
             const data_type &leftData, 
             const data_type &rightData) {
  total_branch_count++;
  assert(n && !n->left && !n->right);
  n->data = 0;
  n->left = new Node(n, leftData);
  n->right = new Node(n, rightData);
  return std::make_pair(n->left, n->right);
}

//returns instruction count for whole tree
int ProfileTree::postorder(ProfileTreeNode* p, int indent){
  int sub = 0;
  if(p != NULL) {
    if(p->left)  sub += postorder(p->left, indent+4);
    if(p->right) sub += postorder(p->right, indent+4);
    if (indent) {
      std::cout << std::setw(indent) << ' ';
    }
    std::cout << "number of instructions " <<p->ins_count << "\n";
    sub += p->ins_count; 
  }
  return sub;
}

ProfileTreeNode::ProfileTreeNode(ProfileTreeNode *_parent, 
                     ExecutionState *_data) 
  : parent(_parent),
    left(0),
    right(0),
    data(_data),
    condition(0),
    ins_count(0) {
}

ProfileTreeNode::~ProfileTreeNode() {
}

int  ProfileTree::get_total_branch_count(void){ return total_branch_count; }
int  ProfileTreeNode::get_ins_count(void){ return ins_count; }
int  ProfileTreeNode::get_total_ins_count(void){ return total_ins_count; }
void ProfileTreeNode::increment_ins_count(void){
  total_ins_count++;
  ins_count++;
}
