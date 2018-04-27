//===-- ProfileTree.cpp ---------------------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "ProfileTree.h"
#include "klee/ExecutionState.h"

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
int ProfileTreeNode::total_clone_count = 0;
int ProfileTreeNode::total_function_call_count = 0;

ProfileTree::ProfileTree(const data_type &_root) : root(new Node(0, _root, NULL)) {
}

ProfileTree::~ProfileTree() {}

ProfileTreeNode*
ProfileTreeNode::link(
             ExecutionState* data,
             llvm::Instruction* ins) {
  assert(this->data            == data);
  assert(this->children.size() == 0);
  assert(this->my_type         == function_parent);

  this->data = 0;
  ProfileTreeNode* kid  = new ProfileTreeNode(this, data, ins);
  this->children.push_back(kid);
  return kid;
}

void ProfileTreeNode::function_call(
             ExecutionState* data,
             llvm::Instruction* ins) {
  total_function_call_count++;
  assert(this->my_type == leaf);
  this->my_type         = function_parent;
  ProfileTreeNode* kid  = link(data, ins);
  data->profiletreeNode = kid;

  assert(data  == kid->data);
  assert(kid->parent == this);
}

std::pair<ProfileTreeNode*, ProfileTreeNode*>
ProfileTreeNode::split(
             ExecutionState* leftData,
             ExecutionState* rightData,
             llvm::Instruction* ins) {
  assert(this->children.size() == 0);
  assert(this->my_type != leaf);
  this->data = 0;
  ProfileTreeNode* left  = new ProfileTreeNode(this, leftData, ins);
  ProfileTreeNode* right = new ProfileTreeNode(this, rightData, ins);
  this->children.push_back(left);
  this->children.push_back(right);
  return std::make_pair(left, right);
}

std::pair<ProfileTreeNode*, ProfileTreeNode*>
ProfileTreeNode::branch(
             ExecutionState* leftData,
             ExecutionState* rightData,
             llvm::Instruction* ins) {
  total_branch_count++;
  assert(leftData != rightData);
  assert(this->my_type == leaf);
  this->my_type = branch_parent;
  std::pair<ProfileTreeNode*, ProfileTreeNode*> ret = split(leftData, rightData, ins);
  leftData->profiletreeNode = ret.first;
  rightData->profiletreeNode = ret.second;

  assert(leftData  == ret.first->data);
  assert(rightData == ret.second->data);
  assert(ret.first->parent == ret.second->parent);

  return ret;
}

std::pair<ProfileTreeNode*, ProfileTreeNode*>
ProfileTreeNode::clone(
             ExecutionState* me_state,
             ExecutionState* clone_state,
             llvm::Instruction* ins) {
  assert(this->my_type == leaf);
  assert(this == me_state->profiletreeNode);
  assert(this->data == me_state);
  assert(me_state != clone_state);
  assert(this->children.size() == 0);

  total_clone_count++;
  std::pair<ProfileTreeNode*, ProfileTreeNode*> ret;
  if (this->parent == NULL) { //Root case
    assert(this->get_ins_count() == 0);

    this->my_type = clone_parent;
    ret = this->split(me_state, clone_state, ins);
  } else if (this->get_ins_count() > 0 ||
      this->parent->my_type == function_parent ) { //Split the current node
    this->my_type = clone_parent;
    ret = this->split(me_state, clone_state, ins);
  } else if (this->get_ins_count() == 0) { //make sibling and add to parent
    assert(this->parent->my_type == clone_parent);

    ProfileTreeNode* clone_node = new ProfileTreeNode(this->parent, clone_state, ins);
    this->parent->children.push_back(clone_node);
    ret = std::make_pair(this, clone_node);
  } else {
    assert(0);
  }
  assert(ret.first != ret.second);
  assert(me_state    == ret.first->data);
  assert(clone_state == ret.second->data);
  assert(ret.first->parent == ret.second->parent);

  me_state->profiletreeNode = ret.first;
  clone_state->profiletreeNode = ret.second;
  return ret;
}

//returns instruction count for whole tree
int ProfileTree::postorder(ProfileTreeNode* p, int indent){
  int sub = 0; //records the number of instructions
  if(p != NULL) {
    //Recurse for children:
    std::vector <ProfileTreeNode*> :: iterator i;
    for (i = p->children.begin(); i != p->children.end(); ++i) {
      sub += postorder(*i, indent + 4);
    }

    //Printing for this node:
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
    my_type(leaf),
    my_instruction(_ins){
}

ProfileTreeNode::~ProfileTreeNode() {
}

int  ProfileTreeNode::get_total_branch_count(void){ return total_branch_count; }
int  ProfileTreeNode::get_ins_count(void){ return ins_count; }
int  ProfileTreeNode::get_total_ins_count(void){ return total_ins_count; }
int  ProfileTreeNode::get_total_clone_count(void){ return total_clone_count; }
void ProfileTreeNode::increment_ins_count(void){
  total_ins_count++;
  ins_count++;
}
enum ProfileTreeNode::NodeType  ProfileTreeNode::get_type(void){ return my_type; }
llvm::Instruction* ProfileTreeNode::get_instruction(void){ return my_instruction; }
