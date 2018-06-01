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

///////////////////////////////////////////////////////////////////////////////
///////////////////////// Tree Additions //////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

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

  //remove this from the branch or clone
  std::vector<ProfileTreeNode*>* v= &(((ContainerBranchClone*)this->my_branch_or_clone->container)->my_branches_or_clones);
  auto first = std::find(v->begin(), v->end(), this);
  assert(*first == this);
  v->erase(first);

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
  //remove this from the branch or clone
  std::vector<ProfileTreeNode*>* v= &(((ContainerBranchClone*)this->my_branch_or_clone->container)->my_branches_or_clones);
  auto first = std::find(v->begin(), v->end(), this);
  assert(*first == this);
  v->erase(first);

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
    this->container = new ContainerBranchClone(ins);
    ret = this->split(me_state, clone_state);
    assert(ret.first->my_branch_or_clone == this);
    assert(ret.second->my_branch_or_clone == this);
  } else if (this->get_ins_count() == 0) { //make sibling and add to parent
    assert(this->parent->my_type == clone_parent);
    assert(parent == my_branch_or_clone);

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

///////////////////////////////////////////////////////////////////////////////
///////////////////// Processing And Traversal ////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

//Returns instruction count for whole tree
#define DFS_DEBUG 0
int ProfileTree::dfs(ProfileTreeNode *root){
  //this updates all the function nodes with the instruction statistics for
  //the functions they call. Must be called before
  //postorder_function_update_statistics().
  root->update_subtree_count();
  std::cout << "\nPostorder Function Statistics\n";
  root->update_function_statistics();
  //Tree statistic collection:
  int nodes_traversed = 0;
  int total_instr = 0; //records the number of instructions

  std::stack <ProfileTreeNode*> nodes_to_visit;
  nodes_to_visit.push(root); //add children to the end
  ProfileTreeNode* winner = NULL; 
  while( nodes_to_visit.size() > 0 ) {
    //Handling DFS traversal:
    ProfileTreeNode* p = nodes_to_visit.top(); //get last element
    nodes_to_visit.pop(); //remove last element

    for (auto i = p->children.begin(); i != p->children.end(); ++i)
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
        winner = p;
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
  consolidate_function_data();
  winner->process_winner_parents();
  printf("total_winners %d\n",root->total_winners );

  make_sibling_lists();
  return total_instr;
}

static bool customCompare(ProfileTreeNode* x, ProfileTreeNode* y){
  return (x->get_depth() <= y->get_depth());
}


void ProfileTree::make_sibling_lists(void){
  std::vector <ProfileTreeNode*> v;
  v.push_back(root);
  while(v.size() > 0){
    ProfileTreeNode* n = v[0];
    v.erase(v.begin());
    //Initialize n's siblings:
    for(auto i = v.begin(); i != v.end(); i++){
      if((*i)->parent){
        assert(n->get_depth() >= (*i)->parent->get_depth());
      }
      assert(n->get_depth() <= (*i)->get_depth());
      n->siblings.push_back(*i);

      //if *i and n have the same depth, we want to make sure that they both
      //have pointers to eachother:
      if((*i)->depth == n->depth)
        (*i)->siblings.push_back(n);
    }

    //Add n's first decendents with differing depth
    for(auto i = n->children.begin(); i != n->children.end(); i++){
      //The depth doesn't change between i and n, i gets n's siblings, and we
      //do not search i in the bfs search.
      //Search for decendents of n with greater depth.
      std::vector <ProfileTreeNode*> decendents;
      decendents.push_back(*i);
      while(decendents.size() > 0){
        ProfileTreeNode* d = decendents[0];
        decendents.erase(decendents.begin());
        //if the depth is the same:
        //  set the child's siblings be the same as the parent's
        //  add child to the search.
        if(d->get_depth() == d->parent->get_depth()){
          d->siblings = d->parent->siblings;
          for(auto j = d->children.begin(); j != d->children.end(); j++){
            decendents.push_back(*j);
          }
        //Otherwise add the child to the general search
        } else {
          v.push_back(d);
        }
      }
    }
    sort(v.begin(), v.end(), customCompare);
  }
}

void ProfileTreeNode::process_winner_parents(){
  assert(winner);

  ProfileTreeNode* ancestor = parent;
  while(ancestor != NULL){
    assert(ancestor->get_winner() == false);
    ancestor->set_winner();
    ancestor = ancestor->parent;
  }
}

void ProfileTreeNode::update_subtree_count(void){
  sub_tree_ins_count = 0;
  for (auto i = children.begin(); i != children.end(); ++i) {
    (*i)->update_subtree_count();
    sub_tree_ins_count += (*i)->sub_tree_ins_count;
    sub_tree_ins_count += (*i)->ins_count;
  }
}


#define PRINT_FUNC_STATS 0
void ProfileTreeNode::update_function_statistics(){
  //Recurse for children
  if(my_type == call_ins){
    ContainerCallIns* call_container = ((ContainerCallIns*)container);
    for (auto i = call_container->my_calls.begin(); i != call_container->my_calls.end(); ++i) {
      assert((*i)->my_type == call_ins);
      (*i)->update_function_statistics();
    }
    for (auto i = call_container->my_calls.begin(); i != call_container->my_calls.end(); ++i) {
      ContainerCallIns* ic = ((ContainerCallIns*)(*i)->container);
      call_container->function_calls_ins_count    += ic->function_ins_count;
      call_container->function_calls_ins_count    += ic->function_calls_ins_count;
      call_container->function_calls_branch_count += ic->function_branch_count;
      call_container->function_calls_branch_count += ic->function_calls_branch_count;
    }
    assert(call_container->my_target != NULL);
    const char *function_name = call_container->my_target->getName().data();
#if PRINT_FUNC_STATS
    std::cout << function_name << " my ins "
      << call_container->function_ins_count << " subtree ins "
      << call_container->function_calls_ins_count << " my symbolic branches "
      << call_container->function_branch_count << " subtree symbolic branches "
      << call_container->function_calls_branch_count << "\n";
#endif
  } else {
    for (auto i = children.begin(); i != children.end(); ++i) {
      (*i)->update_function_statistics();
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

#define RECORD_ONLY_SSH_FUNCTION_STATS 1
void ProfileTree::consolidate_function_data(){
  std::unordered_map<std::string, FunctionStatstics*> stats;
  std::stack <ProfileTreeNode*> nodes_to_visit;
  nodes_to_visit.push(root); //add children to the end
  while( nodes_to_visit.size() > 0 ) {
    //Handling DFS traversal:
    ProfileTreeNode* p = nodes_to_visit.top(); //get last element
    nodes_to_visit.pop(); //remove last element

    if(p->get_type() == ProfileTreeNode::NodeType::call_ins){
      ContainerCallIns* c = (ContainerCallIns*) p->container;
      for (auto i = c->my_calls.begin(); i != c->my_calls.end(); ++i)
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
      for (auto i = p->children.begin(); i != p->children.end(); ++i)
        nodes_to_visit.push(*i); //add children
    }
  }
  std::cout << "\nConsolidated Function Data: \n";
  for (auto itr = stats.begin(); itr != stats.end(); itr++) {
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


///////////////////////////////////////////////////////////////////////////////
/////////////////////// Constructors //////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

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
    edge_ins_count(0),
    depth(0),
    my_type(leaf),
    my_function(0),
    winner(false){
      my_node_number = total_node_count;
      if(_parent == NULL){
        my_type = root;
      } else {
        assert(_parent->my_node_number < my_node_number);
        depth = _parent->depth;
        if(_parent->winner){
          assert(!_parent->parent->winner);
          set_winner();
        }

        //handle branch or clone we belong to:
        if(_parent->my_type == branch_parent || _parent->my_type == clone_parent){
          my_branch_or_clone = _parent;
        } else {
          my_branch_or_clone = _parent->my_branch_or_clone;
          edge_ins_count = parent->edge_ins_count;
        }
        ((ContainerBranchClone*)my_branch_or_clone->container)->my_branches_or_clones.push_back(this);

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


///////////////////////////////////////////////////////////////////////////////
////////////////// Getters, Setters, Incrimenting /////////////////////////////
///////////////////////////////////////////////////////////////////////////////

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
  depth++;
  edge_ins_count++;
  if(parent)
    assert(depth == ins_count + parent->depth);
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

int ProfileTreeNode::get_depth() { return depth; }

///////////////////////////////////////////////////////////////////////////////
///////////////////////// Write Graphs ////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////


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
      for (auto i = ((ContainerCallIns*)n->container)->my_calls.begin(); i != ((ContainerCallIns*)n->container)->my_calls.end(); ++i){
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
      for (auto i = n->children.begin(); i != n->children.end(); ++i){
        os << "\tn" << n << " -> n" << *i << ";\n";
        stack.push_back(*i); //add children
      }
    }
  }
  os << "}\n";
  delete pp;
  delete &os;
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

    if(n->my_type == ProfileTreeNode::branch_parent || n->my_type == ProfileTreeNode::clone_parent){
      os << "\tn" << n << " [label=" << n->sub_tree_ins_count;
    }else{
      os << "\tn" << n << " [label=\"\"";
    }

    if(n->get_winner())
      os << ",fillcolor=red";
    else if(n->my_type == ProfileTreeNode::branch_parent)
      os << ",fillcolor=green";
    else if (n->my_type == ProfileTreeNode::clone_parent)
      os << ",fillcolor=yellow";
    os << "];\n";

    if(n->my_type == ProfileTreeNode::branch_parent || n->my_type == ProfileTreeNode::clone_parent){
      for (auto i = ((ContainerBranchClone*)n->container)->my_branches_or_clones.begin(); i != ((ContainerBranchClone*)n->container)->my_branches_or_clones.end(); ++i){
        assert(*i != NULL);
        //these lines revert it back to the origional graph of just clones and
        //branches:
        //if((*i)->my_type == ProfileTreeNode::leaf)
        //  continue;
        os << "\tn" << n << " -> n" << *i << ";\n";
        stack.push_back(*i); //add children
      }
    }else{
      for (auto i = n->children.begin(); i != n->children.end(); ++i){
        assert(*i != NULL);
        os << "\tn" << n << " -> n" << *i << ";\n";
        stack.push_back(*i); //add children
      }
    }
  }
  os << "}\n";
  delete pp;
  delete &os;
}
