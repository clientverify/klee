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
#include "llvm/IR/Function.h"
#include "llvm/IR/BasicBlock.h"
#include <iostream>
#include <iomanip>

#include <vector>

using namespace klee;
int ProfileTreeNode::total_ins_count = 0;
int ProfileTreeNode::total_branch_count = 0;

ProfileTree::ProfileTree(const data_type &_root) : root(new Node(0, _root, NULL)) {
}

ProfileTree::~ProfileTree() {}


std::pair<ProfileTreeNode*, ProfileTreeNode*>
ProfileTreeNode::split(
             ExecutionState* leftData,
             ExecutionState* rightData,
             llvm::Instruction* ins) {
  total_branch_count++;
  assert(this->children.size() == 0);
  this->data = 0;
  ProfileTreeNode* left  = new ProfileTreeNode(this, leftData, ins);
  ProfileTreeNode* right = new ProfileTreeNode(this, rightData, ins);
  this->children.push_back(left);
  this->children.push_back(right);
  return std::make_pair(left, right);
}

//returns instruction count for whole tree
int ProfileTree::postorder(ProfileTreeNode* p, int indent){
  int sub = 0;
  if(p != NULL) {
    std::vector <ProfileTreeNode*> :: iterator i;
    for (i = p->children.begin(); i != p->children.end(); ++i)
      sub += postorder(*i, indent + 4);
    if (indent) {
      std::cout << std::setw(indent) << ' ';
    }
    if(p->my_instruction != NULL) {
      std::string function_name(p->my_instruction->getParent()->getParent()->getName().data());
      std::cout << "function name: " << function_name << " ";
    } else {
      assert(p == this->root);
    }
    std::cout << "number of instructions " <<p->ins_count << "\n";
    sub += p->ins_count; 
  }
  return sub;
}

ProfileTreeNode::ProfileTreeNode(ProfileTreeNode *_parent, 
                     ExecutionState *_data, llvm::Instruction *_ins)
  : parent(_parent),
    children(),
    data(_data),
    condition(0),
    ins_count(0),
    my_instruction(_ins){
}

ProfileTreeNode::~ProfileTreeNode() {
}

int  ProfileTreeNode::get_total_branch_count(void){ return total_branch_count; }
int  ProfileTreeNode::get_ins_count(void){ return ins_count; }
int  ProfileTreeNode::get_total_ins_count(void){ return total_ins_count; }
void ProfileTreeNode::increment_ins_count(void){
  total_ins_count++;
  ins_count++;
}
