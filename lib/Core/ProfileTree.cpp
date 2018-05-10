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
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/DebugInfo.h"
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
int ProfileTreeNode::total_winners = 0;

ProfileTree::ProfileTree(const data_type &_root) : root(new Node(0, _root)) {
}

ProfileTree::~ProfileTree() {}

ProfileTreeNode*
ProfileTreeNode::link(
             ExecutionState* data) {
  assert(this->data            == data);
  assert(this->children.size() == 0);
  assert(this->my_type == call_ins || this->my_type == return_ins);

  this->data = 0;
  ProfileTreeNode* kid  = new ProfileTreeNode(this, data);
  this->children.push_back(kid);
  return kid;
}

#define DEBUG_FUNCTION_DIR 0
const char* get_instruction_directory(llvm::Instruction* target_ins){
  assert(target_ins  != NULL);
  const char *function_name = target_ins->getParent()->getParent()->getName().data();
  assert(function_name  != NULL);
  llvm::MDNode *metadata = target_ins->getMetadata("dbg");
  if (!metadata) {
    if(DEBUG_FUNCTION_DIR) printf("function_call/return not adding info for %s no metadata\n", function_name);
    return NULL;
  }

  llvm::DILocation loc(metadata); // DILocation is in DebugInfo.h
  const char  *dir  = loc.getDirectory().data();
  return dir;
}

const char* get_function_directory(llvm::Function* target){
  assert(target != NULL);
  const char *target_name = target->getName().data();
  assert(target_name != NULL);
  if(target->size() <= 0){
    if(DEBUG_FUNCTION_DIR) printf("function_call %s has no basic blocks\n", target_name);
    return NULL;
  }
  llvm::Instruction *target_ins = target->getEntryBlock().begin();
  assert(target_ins  != NULL);
  return get_instruction_directory(target_ins);
}

#define FUNC_NODE_DIR "/playpen/cliver0/src/openssh"
#define MODEL_NODE_DIR "/playpen/cliver0/build/klee/runtime/cloud9-POSIX"

void ProfileTreeNode::function_call(
             ExecutionState* data,
             llvm::Instruction* ins,
             llvm::Function* target) {
  assert(target != NULL);
  assert(this->my_type == leaf);

  //llvm.va_start has no basic blocks and no return.
  //External functions are executed (not interpreted) in klee.
  //They don't have a return, which breaks the call return semantics
  //for the tree.
  if( target->size() <= 0 ||
      (target->isDeclaration() &&
      target->getIntrinsicID() == llvm::Intrinsic::not_intrinsic)){
    return;
  }


  total_function_call_count++;
  this->my_type         = call_ins;
  //add myself to my_function's list of calls
  if(this->my_function) {
    this->my_function->my_calls.push_back(this);
  }
  this->my_instruction  = ins;
  this->my_target          = target;
  ProfileTreeNode* kid  = link(data);
  data->profiletreeNode = kid;

  assert(data  == kid->data);
  assert(kid->parent == this);
  if(my_function && my_function->my_target)
    assert(ins->getParent()->getParent() == my_function->my_target);
}

void ProfileTreeNode::function_return(
             ExecutionState* data,
             llvm::Instruction* ins,
             llvm::Instruction* to) {
  assert(this->my_type == leaf);

  total_function_ret_count++;
  this->my_type         = return_ins;
  this->my_instruction  = ins;
  this->my_return_to    = to;
  ProfileTreeNode* kid  = link(data);
  data->profiletreeNode = kid;

  assert(data  == kid->data);
  assert(kid->parent == this);
  if(my_function && my_function->my_target)
    assert(ins->getParent()->getParent() == my_function->my_target);
}

std::pair<ProfileTreeNode*, ProfileTreeNode*>
ProfileTreeNode::split(
             ExecutionState* leftData,
             ExecutionState* rightData) {
  assert(this->children.size() == 0);
  assert(this->my_type != leaf);
  this->data = 0;
  ProfileTreeNode* left  = new ProfileTreeNode(this, leftData);
  ProfileTreeNode* right = new ProfileTreeNode(this, rightData);
  this->children.push_back(left);
  this->children.push_back(right);
  return std::make_pair(left, right);
}

void ProfileTreeNode::branch(
             ExecutionState* leftData,
             ExecutionState* rightData,
             llvm::Instruction* ins) {
  this->increment_branch_count();
  assert(leftData != rightData);
  assert(this->my_type == leaf);
  this->my_type = branch_parent;
  this->my_instruction  = ins;
  std::pair<ProfileTreeNode*, ProfileTreeNode*> ret = split(leftData, rightData);
  leftData->profiletreeNode = ret.first;
  rightData->profiletreeNode = ret.second;

  assert(leftData  == ret.first->data);
  assert(rightData == ret.second->data);
  assert(ret.first->parent == ret.second->parent);

  if(my_function && my_function->my_target)
    assert(ins->getParent()->getParent() == my_function->my_target);
}

void ProfileTreeNode::clone(
             ExecutionState* me_state,
             ExecutionState* clone_state,
             llvm::Instruction* ins) {
  assert(this->my_type == leaf || this->my_type == root);
  assert(this == me_state->profiletreeNode);
  assert(this->data == me_state);
  assert(me_state != clone_state);
  assert(this->children.size() == 0);

  total_clone_count++;
  std::pair<ProfileTreeNode*, ProfileTreeNode*> ret;
  if (this->parent == NULL) { //Root case
    assert(this->get_ins_count() == 0);

    this->my_type = clone_parent;
    this->my_instruction  = ins;
    ret = this->split(me_state, clone_state);
  } else if (this->get_ins_count() > 0 ||
      this->parent->my_type == call_ins ) { //Split the current node
    this->my_type = clone_parent;
    this->my_instruction  = ins;
    ret = this->split(me_state, clone_state);
  } else if (this->get_ins_count() == 0) { //make sibling and add to parent
    assert(this->parent->my_type == clone_parent);

    ProfileTreeNode* clone_node = new ProfileTreeNode(this->parent, clone_state);
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
  if(my_function && my_function->my_target)
    assert(ins->getParent()->getParent() == my_function->my_target);
}

//Returns instruction count for whole tree
#define DFS_DEBUG 0
int ProfileTree::dfs(ProfileTreeNode *root){
  //this updates all the function nodes with the instruction statistics for
  //the functions they call.
  root->postorder_function_update_statistics();
  //Tree statistic collection:
  int nodes_traversed = 0;
  int total_instr = 0; //records the number of instructions

  std::stack <ProfileTreeNode*> nodes_to_visit;
  nodes_to_visit.push(root); //add children to the end
  while( nodes_to_visit.size() > 0 ) {
    //Handling DFS traversal:
    ProfileTreeNode* p = nodes_to_visit.top(); //get last element
    nodes_to_visit.pop(); //remove last element

    std::vector <ProfileTreeNode*> :: iterator i;
    for (i = p->children.begin(); i != p->children.end(); ++i)
      nodes_to_visit.push(*i); //add children

    //Statistics:
    total_instr += p->ins_count;
    nodes_traversed++;

    //Asserts and print outs (looking inside the node):
    assert(p != NULL);
    if(p->parent) assert(p->parent->my_node_number < p->my_node_number);
    if(p->get_type() != ProfileTreeNode::NodeType::return_ins)
      assert(p->my_return_to == NULL);
    if(p->get_type() != ProfileTreeNode::NodeType::call_ins)
      assert(p->my_target == NULL);
    if(p->get_winner()){
      if(p->get_type() == ProfileTreeNode::NodeType::clone_parent){
        assert(p->children.size() == 2);
      }else{
        assert(p->ins_count == 0);
        assert(p->get_type() == ProfileTreeNode::NodeType::leaf);
        assert(p->parent->get_winner());
      }
    }

    if(DFS_DEBUG) printf("dfs node#: %d children: %d type: ", p->my_node_number, p->children.size());
    switch(p->get_type()) {
      case ProfileTreeNode::NodeType::root:
        assert(p->my_instruction == NULL);
        assert(p->my_calls.size() == 0);
        if(DFS_DEBUG) printf("root ");
        break;
      case ProfileTreeNode::NodeType::leaf:
        assert(p->my_instruction == NULL);
        assert(p->children.size() == 0);
        assert(p->my_calls.size() == 0);
        if(DFS_DEBUG) printf("leaf ");
        break;
      case ProfileTreeNode::NodeType::branch_parent:
        assert(p->my_instruction != NULL);
        assert(p->children.size() == 2);
        assert(p->my_calls.size() == 0);
        if(DFS_DEBUG) printf("branch ");
        break;
      case ProfileTreeNode::NodeType::return_ins:
        assert(p->my_instruction != NULL);
        assert(p->children.size() == 1);
        assert(p->my_return_to != NULL);
        assert(p->my_calls.size() == 0);
        if(DFS_DEBUG) printf("return ");
        break;
      case ProfileTreeNode::NodeType::call_ins:
        assert(p->my_instruction != NULL);
        assert(p->children.size() == 1);
        assert(p->my_target != NULL);
        if(DFS_DEBUG) printf("call ");
        break;
      case ProfileTreeNode::NodeType::clone_parent:
        assert(p->my_instruction != NULL);
        assert(p->children.size() > 0);
        assert(p->my_calls.size() == 0);
        if(DFS_DEBUG) printf("clone ");
        break;
      default:
        assert(0);
    }

    if(p->my_instruction != NULL) {
      const char *function_name = p->my_instruction->getParent()->getParent()->getName().data();
      if(DFS_DEBUG) printf("function name: %s", function_name);
    }

    if(DFS_DEBUG) printf("\n");
  }
  printf("total_winners %d\n",root->total_winners );
  return total_instr;
}

void ProfileTreeNode::postorder_function_update_statistics(){
  //Recurse for children
  if(my_type == call_ins){
    std::vector <ProfileTreeNode*> :: iterator i;
    for (i = my_calls.begin(); i != my_calls.end(); ++i) {
      assert((*i)->my_type == call_ins);
      (*i)->postorder_function_update_statistics();
    }
    for (i = my_calls.begin(); i != my_calls.end(); ++i) {
      function_calls_ins_count += (*i)->function_ins_count;
      function_calls_ins_count += (*i)->function_calls_ins_count;
    }
    const char *function_name = my_instruction->getParent()->getParent()->getName().data();
    std::cout << function_name << " instructions executed in this function "
      << function_ins_count << " instructions executed in subtree "
      << function_calls_ins_count << "\n";

  } else {
    std::vector <ProfileTreeNode*> :: iterator i;
    for (i = children.begin(); i != children.end(); ++i) {
      (*i)->postorder_function_update_statistics();
    }
  }
}


ProfileTreeNode::ProfileTreeNode(ProfileTreeNode *_parent, 
                     ExecutionState *_data)
  : parent(_parent),
    children(),
    my_calls(),
    data(_data),
    ins_count(0),
    function_ins_count(0), //only used by call node, keeps track of instructions executed in target from this call
    function_calls_ins_count(0), //only used by call node, keeps track of all instructions executed in the functions this function calls
    my_type(leaf),
    my_instruction(0),
    my_target(0), //target is only for function call nodes
    my_return_to(0),
    my_function(0),
    winner(false){
      my_node_number = total_node_count;
      if(_parent == NULL){
        my_type = root;
      } else {
        assert(_parent->my_node_number < my_node_number);
        if(_parent->winner){
          assert(!_parent->parent->winner);
          set_winner();
        }

        //handle function we belong to:
        if(_parent->my_type == call_ins){
          my_function = _parent;
        } else if ( _parent->my_function == NULL ){
          my_function = NULL;
        } else if(_parent->my_type == return_ins){
          my_function = _parent->my_function->my_function;
        } else {
          my_function = _parent->my_function;
        }

      }
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
void ProfileTreeNode::increment_ins_count(llvm::Instruction *i){
  assert(i != NULL);
  if(my_function != NULL){
    assert(my_function->my_target != NULL);
    assert(i->getParent()->getParent() == my_function->my_target);
    my_function->function_ins_count++;
  }
  total_ins_count++;
  ins_count++;
}
void ProfileTreeNode::increment_branch_count(void){
  total_branch_count++;
}
enum ProfileTreeNode::NodeType  ProfileTreeNode::get_type(void){ return my_type; }
llvm::Instruction* ProfileTreeNode::get_instruction(void){ return my_instruction; }
bool ProfileTreeNode::get_winner(void){ return winner; }
void ProfileTreeNode::set_winner(void){
  total_winners++;
  assert(!winner);
  winner = true;
}
