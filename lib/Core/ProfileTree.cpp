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
int ProfileTreeNode::total_node_count = 0;
int ProfileTreeNode::total_branch_count = 0;
int ProfileTreeNode::total_clone_count = 0;
int ProfileTreeNode::total_function_call_count = 0;
int ProfileTreeNode::total_function_ret_count = 0;

ProfileTree::ProfileTree(const data_type &_root) : root(new Node(0, _root, NULL)) {
}

ProfileTree::~ProfileTree() {}

ProfileTreeNode*
ProfileTreeNode::link(
             ExecutionState* data,
             llvm::Instruction* ins) {
  assert(ins != NULL);
  assert(this->data            == data);
  assert(this->children.size() == 0);
  assert(this->my_type == function_parent || this->my_type == function_return_parent);

  this->data = 0;
  ProfileTreeNode* kid  = new ProfileTreeNode(this, data, ins);
  this->children.push_back(kid);
  return kid;
}

void ProfileTreeNode::function_call(
             ExecutionState* data,
             llvm::Instruction* ins,
             llvm::Function* target) {
  total_function_call_count++;
  assert(target != NULL);
  assert(this->my_type == leaf);
  this->my_type         = function_parent;
  this->my_target          = target;
  ProfileTreeNode* kid  = link(data, ins);
  data->profiletreeNode = kid;

  assert(data  == kid->data);
  assert(kid->parent == this);
}

void ProfileTreeNode::function_return(
             ExecutionState* data,
             llvm::Instruction* ins,
             llvm::Instruction* to) {
  total_function_ret_count++;
  assert(this->my_type == leaf);
  if(this->my_type == function_parent)
    assert(to->getParent() == this->my_instruction->getParent());
  this->my_type         = function_return_parent;
  this->my_return_to    = to;
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

void ProfileTreeNode::branch(
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
}

void ProfileTreeNode::clone(
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
}

//returns instruction count for whole tree
//I think this might be a recursive function using all of the stack space?
int ProfileTree::postorder(ProfileTreeNode* p, int indent){
  static int nodes_traversed = 0;
  if(p->parent) assert(p->parent->my_node_number < p->my_node_number);
  nodes_traversed++;

  printf("postorder nodes: %d children %d type ", p->my_node_number, p->children.size());
  if(p->get_type() == ProfileTreeNode::NodeType::leaf){
    assert(p->children.size() == 0);
    printf("leaf\n");
  }
  if(p->get_type() == ProfileTreeNode::NodeType::branch_parent){
    assert(p->children.size() == 2);
    printf("branch\n");
  }
  if(p->get_type() == ProfileTreeNode::NodeType::function_return_parent){
    assert(p->children.size() == 1);
    assert(p->my_return_to != NULL);
    printf("return\n");
  }else
    assert(p->my_return_to == NULL);
  if(p->get_type() == ProfileTreeNode::NodeType::function_parent){
    assert(p->children.size() == 1);
    assert(p->my_target != NULL);
    printf("call\n");
  }else
    assert(p->my_target == NULL);
  if(p->get_type() == ProfileTreeNode::NodeType::clone_parent){
    assert(p->children.size() > 0);
    printf("clone\n");
  }

  int sub = 0; //records the number of instructions
  if(p != NULL) {
    //Recurse for children:
    std::vector <ProfileTreeNode*> :: iterator i;
    for (i = p->children.begin(); i != p->children.end(); ++i) {
      sub += postorder(*i, indent + 4);
    }

    //Printing for this node:
    if (indent) {
      //std::cout << std::setw(indent) << ' ';
    }
    if(p->my_instruction != NULL) {
      std::string function_name(p->my_instruction->getParent()->getParent()->getName().data());
      //std::cout << "function name: " << function_name << " ";
    } else {
      assert(p == this->root);
    }
    //std::cout << "number of instructions " <<p->ins_count << "\n";
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
    my_instruction(_ins),
    my_target(0),
    my_return_to(0){ //target is only for function call nodes
      my_node_number = total_node_count;
      if(_parent) assert(_parent->my_node_number < my_node_number);
      total_node_count++;
}

ProfileTreeNode::~ProfileTreeNode() {
}

int  ProfileTreeNode::get_total_branch_count(void){ return total_branch_count; }
int  ProfileTreeNode::get_ins_count(void){ return ins_count; }
int  ProfileTreeNode::get_total_ins_count(void){ return total_ins_count; }
int  ProfileTreeNode::get_total_node_count(void){ return total_node_count; }
int  ProfileTreeNode::get_total_ret_count(void){ return total_function_ret_count; }
int  ProfileTreeNode::get_total_call_count(void){ return total_function_call_count; }
int  ProfileTreeNode::get_total_clone_count(void){ return total_clone_count; }
void ProfileTreeNode::increment_ins_count(void){
  total_ins_count++;
  ins_count++;
}
enum ProfileTreeNode::NodeType  ProfileTreeNode::get_type(void){ return my_type; }
llvm::Instruction* ProfileTreeNode::get_instruction(void){ return my_instruction; }
