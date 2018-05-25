//===-- ProfileTree.cpp ---------------------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "ProfileTree.h"
#include "Util.h"
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
    ((ContainerCallIns*)this->my_function->container)->my_calls.push_back(this);
  }
  this->container = new ContainerCallIns(ins, target);
  ProfileTreeNode* kid  = link(data);
  data->profiletreeNode = kid;

  assert(data  == kid->data);
  assert(kid->parent == this);
  if(my_function && ((ContainerCallIns*)my_function->container)->my_target)
    assert(ins->getParent()->getParent() == ((ContainerCallIns*)my_function->container)->my_target);
}

void ProfileTreeNode::function_return(
             ExecutionState* data,
             llvm::Instruction* ins,
             llvm::Instruction* to) {
  assert(this->my_type == leaf);

  total_function_ret_count++;
  this->my_type         = return_ins;
  this->container = new ContainerRetIns(ins, to);
  ProfileTreeNode* kid  = link(data);
  data->profiletreeNode = kid;

  assert(data  == kid->data);
  assert(kid->parent == this);
  if(my_function && ((ContainerCallIns*)my_function->container)->my_target)
    assert(ins->getParent()->getParent() == ((ContainerCallIns*)my_function->container)->my_target);
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
  ((ContainerBranchClone*)this->my_branch_or_clone->container)->my_branches_or_clones.push_back(this);
  this->container = new ContainerBranchClone(ins);
  std::pair<ProfileTreeNode*, ProfileTreeNode*> ret = split(leftData, rightData);
  leftData->profiletreeNode = ret.first;
  rightData->profiletreeNode = ret.second;

  assert(ret.first->my_branch_or_clone == this);
  assert(ret.second->my_branch_or_clone == this);
  assert(leftData  == ret.first->data);
  assert(rightData == ret.second->data);
  assert(ret.first->parent == ret.second->parent);

  if(my_function && ((ContainerCallIns*)my_function->container)->my_target)
    assert(ins->getParent()->getParent() == ((ContainerCallIns*)my_function->container)->my_target);
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
    this->container = new ContainerBranchClone(ins);
    ret = this->split(me_state, clone_state);
    assert(ret.first->my_branch_or_clone == this);
    assert(ret.second->my_branch_or_clone == this);
  } else if (this->get_ins_count() > 0 ||
      this->parent->my_type == call_ins ) { //Split the current node
    this->my_type = clone_parent;
    assert(my_branch_or_clone != NULL);
    ((ContainerBranchClone*)this->my_branch_or_clone->container)->my_branches_or_clones.push_back(this);
    this->container = new ContainerBranchClone(ins);
    ret = this->split(me_state, clone_state);
    assert(ret.first->my_branch_or_clone == this);
    assert(ret.second->my_branch_or_clone == this);
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
  if(my_function && ((ContainerCallIns*)my_function->container)->my_target)
    assert(ins->getParent()->getParent() == ((ContainerCallIns*)my_function->container)->my_target);
}

//Returns instruction count for whole tree
#define DFS_DEBUG 0
int ProfileTree::dfs(ProfileTreeNode *root){
  //this updates all the function nodes with the instruction statistics for
  //the functions they call.
  std::cout << "\nPostorder Function Statistics\n";
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
    if(p->get_type() == ProfileTreeNode::NodeType::return_ins)
      assert(dynamic_cast<ContainerRetIns*>(p->container) != NULL);
    if(p->get_type() == ProfileTreeNode::NodeType::call_ins)
      assert(dynamic_cast<ContainerCallIns*>(p->container) != NULL);
    if(p->get_winner()){
      if(p->get_type() == ProfileTreeNode::NodeType::clone_parent){
        assert(p->children.size() == 2);
      }else{
        assert(p->ins_count == 0);
        assert(p->get_type() == ProfileTreeNode::NodeType::leaf);
        assert(p->parent->get_winner());
      }
    }
    if(p->get_type() == ProfileTreeNode::NodeType::call_ins){
      assert(((ContainerCallIns*)p->container)->function_calls_branch_count <= p->total_branch_count);
      assert(((ContainerCallIns*)p->container)->function_branch_count <= p->total_branch_count);
    }

    if(DFS_DEBUG) printf("dfs node#: %d children: %d type: ", p->my_node_number, p->children.size());
    switch(p->get_type()) {
      case ProfileTreeNode::NodeType::root:
        assert(p->container == NULL);
        if(DFS_DEBUG) printf("root ");
        break;
      case ProfileTreeNode::NodeType::leaf:
        assert(p->container == NULL);
        assert(p->children.size() == 0);
        if(DFS_DEBUG) printf("leaf ");
        break;
      case ProfileTreeNode::NodeType::branch_parent:
        assert(p->get_instruction() != NULL);
        assert(p->children.size() == 2);
        if(DFS_DEBUG) printf("branch ");
        break;
      case ProfileTreeNode::NodeType::return_ins:
        assert(p->get_instruction() != NULL);
        assert(p->children.size() == 1);
        assert(p->container != NULL);
        if(DFS_DEBUG) printf("return ");
        break;
      case ProfileTreeNode::NodeType::call_ins:
        assert(p->get_instruction() != NULL);
        assert(p->children.size() == 1);
        assert(((ContainerCallIns*)p->container)->my_target != NULL);
        if(DFS_DEBUG) printf("call ");
        break;
      case ProfileTreeNode::NodeType::clone_parent:
        assert(p->get_instruction() != NULL);
        assert(p->children.size() > 0);
        if(DFS_DEBUG) printf("clone ");
        break;
      default:
        assert(0);
    }

    if(p->container != NULL) {
      const char *function_name = p->get_instruction()->getParent()->getParent()->getName().data();
      if(DFS_DEBUG) printf("function name: %s", function_name);
    }

    if(DFS_DEBUG) printf("\n");
  }
  consolidateFunctionData();
  printf("total_winners %d\n",root->total_winners );
  int num_branches = root->postorder_branch_or_clone_count();
  printf("dfs check: total_branches %d\n", num_branches);
  return total_instr;
}

//Returns the total number of branches/clones
int ProfileTreeNode::postorder_branch_or_clone_count(){
  //Recurse for children
  int ret = 0;
  if(my_type == branch_parent || my_type == clone_parent){
    ret++; //we have another branch or clone
    std::vector <ProfileTreeNode*> :: iterator i;
    for (i = ((ContainerBranchClone*)container)->my_branches_or_clones.begin();
        i != ((ContainerBranchClone*)container)->my_branches_or_clones.end(); ++i) {
      assert((*i)->my_type == branch_parent || (*i)->my_type == clone_parent);
      ret += (*i)->postorder_branch_or_clone_count();
    }
  } else {
    std::vector <ProfileTreeNode*> :: iterator i;
    for (i = children.begin(); i != children.end(); ++i) {
      (*i)->postorder_branch_or_clone_count();
    }
  }
  return ret;
}

#define PRINT_FUNC_STATS 0
void ProfileTreeNode::postorder_function_update_statistics(){
  //Recurse for children
  if(my_type == call_ins){
    std::vector <ProfileTreeNode*> :: iterator i;
    for (i = ((ContainerCallIns*)container)->my_calls.begin(); i != ((ContainerCallIns*)container)->my_calls.end(); ++i) {
      assert((*i)->my_type == call_ins);
      (*i)->postorder_function_update_statistics();
    }
    for (i = ((ContainerCallIns*)container)->my_calls.begin(); i != ((ContainerCallIns*)container)->my_calls.end(); ++i) {
      ContainerCallIns* ic = ((ContainerCallIns*)(*i)->container);
      ((ContainerCallIns*)container)->function_calls_ins_count += ic->function_ins_count;
      ((ContainerCallIns*)container)->function_calls_ins_count += ic->function_calls_ins_count;
      ((ContainerCallIns*)container)->function_calls_branch_count += ic->function_branch_count;
      ((ContainerCallIns*)container)->function_calls_branch_count += ic->function_calls_branch_count;
    }
    assert(((ContainerCallIns*)container)->my_target != NULL);
    const char *function_name = ((ContainerCallIns*)container)->my_target->getName().data();
#if PRINT_FUNC_STATS
    std::cout << function_name << " my ins "
      << ((ContainerCallIns*)container)->function_ins_count << " subtree ins "
      << ((ContainerCallIns*)container)->function_calls_ins_count << " my symbolic branches "
      << ((ContainerCallIns*)container)->function_branch_count << " subtree symbolic branches "
      << ((ContainerCallIns*)container)->function_calls_branch_count << "\n";
#endif
  } else {
    std::vector <ProfileTreeNode*> :: iterator i;
    for (i = children.begin(); i != children.end(); ++i) {
      (*i)->postorder_function_update_statistics();
    }
  }
}

void FunctionStatstics::add(ContainerCallIns* c){
  ins_count += c->function_ins_count;
  sub_ins_count += c->function_calls_ins_count;
  branch_count += c->function_branch_count;
  sub_branch_count += c->function_calls_branch_count;
  times_called++;
  num_called += c->my_calls.size();
  assert(function == c->my_target);
}

#define RECORD_ONLY_SSH_FUNCTION_STATS 0
void ProfileTree::consolidateFunctionData(){
  std::unordered_map<std::string, FunctionStatstics*> stats;
  std::stack <ProfileTreeNode*> nodes_to_visit;
  nodes_to_visit.push(root); //add children to the end
  while( nodes_to_visit.size() > 0 ) {
    //Handling DFS traversal:
    ProfileTreeNode* p = nodes_to_visit.top(); //get last element
    nodes_to_visit.pop(); //remove last element

    std::vector <ProfileTreeNode*> :: iterator i;
    if(p->get_type() == ProfileTreeNode::NodeType::call_ins){
      ContainerCallIns* c = (ContainerCallIns*) p->container;
      for (i = c->my_calls.begin(); i != c->my_calls.end(); ++i)
        nodes_to_visit.push(*i); //add call nodes


      //statistic collection
      std::string key = c->my_target->getName().data();
      std::unordered_map<std::string,FunctionStatstics*>::const_iterator itr
        = stats.find(key);
      if (itr == stats.end()){
        //add new record, this function doesn't exist yet.
        FunctionStatstics* fs = new FunctionStatstics(c);
        stats[key] = fs;
      } else {
        (*itr).second->add(c);
      }
    } else {
      for (i = p->children.begin(); i != p->children.end(); ++i)
        nodes_to_visit.push(*i); //add children
    }
  }
  std::cout << "\nConsolidated Function Data: \n";
  std::unordered_map<std::string,FunctionStatstics*>::const_iterator itr;
  for (itr = stats.begin(); itr != stats.end(); itr++) {
    const char* dir = get_function_directory(itr->second->function);

#if RECORD_ONLY_SSH_FUNCTION_STATS
    // itr works as a pointer to pair<string, double>
    // type itr->first stores the key part  and
    // itr->second stroes the value part
    if(dir)
      std::cout << dir << " ";
    else
      std::cout << "no_dir ";

    std::cout << itr->first <<
      " #times called " << itr->second->times_called <<
      " #child calls " << itr->second->num_called <<
      " ins count " << itr->second->ins_count <<
      " sub_ins_count " << itr->second->sub_ins_count <<
      " branch count " << itr->second->branch_count <<
      " sub_branch_count " << itr->second->sub_branch_count <<
      "\n";
#endif
  }
}


FunctionStatstics::FunctionStatstics(ContainerCallIns* c)
  : ins_count(c->function_ins_count),
    sub_ins_count(c->function_calls_ins_count),
    branch_count(c->function_branch_count),
    sub_branch_count(c->function_calls_branch_count),
    times_called(1),
    num_called(c->my_calls.size()),
    function(c->my_target){
      assert(function != NULL);
}

ContainerNode::ContainerNode(llvm::Instruction* i)
  : my_instruction(i){
    assert(i != NULL);
}

ContainerCallIns::ContainerCallIns(llvm::Instruction* i, llvm::Function* target)
  : ContainerNode(i),
    my_target(target),
    my_calls(),
    function_ins_count(0), //counts instructions executed in target from this call
    function_calls_ins_count(0), //counts instructions executed in this function's subtree
    function_branch_count(0), //counts symbolic branches executed in target from this call
    function_calls_branch_count(0){ //counts symbolic branches executed in subtree
  assert(i != NULL);
  assert(my_target != NULL);
}

ContainerRetIns::ContainerRetIns(llvm::Instruction* i, llvm::Instruction* return_to)
  : ContainerNode(i),
    my_return_to(return_to) {
  assert(i != NULL);
  assert(my_return_to != NULL);
}

ContainerBranchClone::ContainerBranchClone(llvm::Instruction* i)
  : ContainerNode(i),
    my_branches_or_clones() {
  assert(i != NULL);
}

ProfileTreeNode::ProfileTreeNode(ProfileTreeNode *_parent, 
                     ExecutionState *_data)
  : parent(_parent),
    children(),
    container(0),
    data(_data),
    ins_count(0),
    my_type(leaf),
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

        //handle branch or clone we belong to:
        if(_parent->my_type == branch_parent || _parent->my_type == clone_parent){
          my_branch_or_clone = _parent;
        } else {
          my_branch_or_clone = _parent->my_branch_or_clone;
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
    assert(((ContainerCallIns*)my_function->container)->my_target != NULL);
    assert(i->getParent()->getParent() == ((ContainerCallIns*)my_function->container)->my_target);
    ((ContainerCallIns*)my_function->container)->function_ins_count++;
  }
  total_ins_count++;
  ins_count++;
}
void ProfileTreeNode::increment_branch_count(void){
  total_branch_count++;
  assert(((ContainerCallIns*)my_function->container)->function_branch_count >= 0);
  assert(((ContainerCallIns*)my_function->container)->function_branch_count < total_branch_count);
  ((ContainerCallIns*)my_function->container)->function_branch_count++;
}
enum ProfileTreeNode::NodeType  ProfileTreeNode::get_type(void){ return my_type; }
llvm::Instruction* ProfileTreeNode::get_instruction(void){
  assert(container);
  assert(container->my_instruction);
  return container->my_instruction;
}
bool ProfileTreeNode::get_winner(void){ return winner; }
void ProfileTreeNode::set_winner(void){
  total_winners++;
  assert(!winner);
  winner = true;
}


//currently dumps the functions in FUNC_NODE_DIR in the call graph.
void ProfileTree::dump_function_call_graph(std::string path) {
  llvm::raw_ostream &os = *(get_fd_ostream(path));
  ExprPPrinter *pp = ExprPPrinter::create(os);
  pp->setNewline("\\l");
  os << "digraph G {\n";
  os << "\tsize=\"10,7.5\";\n";
  os << "\tratio=fill;\n";
  os << "\trotate=90;\n";
  os << "\tcenter = \"true\";\n";
  os << "\tnode [style=\"filled\",width=.1,height=.1,fontname=\"Terminus\"]\n";
  os << "\tedge [arrowsize=.3]\n";
  std::vector<ProfileTree::Node*> stack;
  stack.push_back(root);
  while (!stack.empty()) {
    ProfileTree::Node *n = stack.back();
    stack.pop_back();
    os << "\tn" << n << " [label=\"\"";
    if (n->data)
      os << ",fillcolor=green";
    os << "];\n";

    if(n->my_type == ProfileTreeNode::call_ins){
      const char* dir = get_function_directory(((ContainerCallIns*)n->container)->my_target);
      std::vector <ProfileTreeNode*> :: iterator i;
      for (i = ((ContainerCallIns*)n->container)->my_calls.begin(); i != ((ContainerCallIns*)n->container)->my_calls.end(); ++i){
        if(dir && strcmp(dir, FUNC_NODE_DIR) == 0) {
          const char* child_dir = get_function_directory(((ContainerCallIns*)(*i)->container)->my_target);
          if(child_dir && strcmp(child_dir, FUNC_NODE_DIR) == 0) {
            os << "\tn" << n << " -> n" << *i << ";\n";
            stack.push_back(*i); //add children
          }
        }else{
          os << "\tn" << n << " -> n" << *i << ";\n";
          stack.push_back(*i); //add children
        }
      }
    }else{
      std::vector <ProfileTreeNode*> :: iterator i;
      for (i = n->children.begin(); i != n->children.end(); ++i){
        os << "\tn" << n << " -> n" << *i << ";\n";
        stack.push_back(*i); //add children
      }
    }
  }
  os << "}\n";
  delete pp;
}

//Writes graph of clone and branch nodes.
void ProfileTree::dump_branch_clone_graph(std::string path) {
  llvm::raw_ostream &os = *(get_fd_ostream(path));
  ExprPPrinter *pp = ExprPPrinter::create(os);
  pp->setNewline("\\l");
  os << "digraph G {\n";
  os << "\tsize=\"10,7.5\";\n";
  os << "\tratio=fill;\n";
  os << "\trotate=90;\n";
  os << "\tcenter = \"true\";\n";
  os << "\tnode [style=\"filled\",width=.1,height=.1,fontname=\"Terminus\"]\n";
  os << "\tedge [arrowsize=.3]\n";
  std::vector<ProfileTree::Node*> stack;
  stack.push_back(root);
  while (!stack.empty()) {
    ProfileTree::Node *n = stack.back();
    stack.pop_back();
    os << "\tn" << n << " [label=\"\"";
    if(n->my_type == ProfileTreeNode::branch_parent)
      os << ",fillcolor=green";
    else
      os << ",fillcolor=blue";
    os << "];\n";

    if(n->my_type == ProfileTreeNode::branch_parent || n->my_type == ProfileTreeNode::clone_parent){
      std::vector <ProfileTreeNode*> :: iterator i;
      for (i = ((ContainerBranchClone*)n->container)->my_branches_or_clones.begin(); i != ((ContainerBranchClone*)n->container)->my_branches_or_clones.end(); ++i){
        os << "\tn" << n << " -> n" << *i << ";\n";
        stack.push_back(*i); //add children
      }
    }else{
      std::vector <ProfileTreeNode*> :: iterator i;
      for (i = n->children.begin(); i != n->children.end(); ++i){
        os << "\tn" << n << " -> n" << *i << ";\n";
        stack.push_back(*i); //add children
      }
    }
  }
  os << "}\n";
  delete pp;
}
