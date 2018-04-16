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

void ProfileTree::dump() {
  llvm::raw_fd_ostream *f;
  std::string Error;
  std::string path = "/playpen/cliver0/processtree.graph";
#if LLVM_VERSION_CODE >= LLVM_VERSION(3,5)
  f = new llvm::raw_fd_ostream(path.c_str(), Error, llvm::sys::fs::F_None);
#elif LLVM_VERSION_CODE >= LLVM_VERSION(3,4)
  f = new llvm::raw_fd_ostream(path.c_str(), Error, llvm::sys::fs::F_Binary);
#else
  f = new llvm::raw_fd_ostream(path.c_str(), Error, llvm::raw_fd_ostream::F_Binary);
#endif
  assert(f);
  if (!Error.empty()) {
    printf("error opening file \"%s\".  KLEE may have run out of file "
        "descriptors: try to increase the maximum number of open file "
        "descriptors by using ulimit (%s).",
        path.c_str(), Error.c_str());
    delete f;
    f = NULL;
  }
  if (f) {
    dump(*f);
    delete f;
  }
}


void ProfileTree::dump(llvm::raw_ostream &os) {
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
    if (n->condition.isNull()) {
      os << "\tn" << n << " [label=\"\"";
    } else {
      os << "\tn" << n << " [label=\"";
      pp->print(n->condition);
      os << "\",shape=diamond";
    }
    if (n->data)
      os << ",fillcolor=green";
    os << "];\n";
    if (n->left) {
      os << "\tn" << n << " -> n" << n->left << ";\n";
      stack.push_back(n->left);
    }
    if (n->right) {
      os << "\tn" << n << " -> n" << n->right << ";\n";
      stack.push_back(n->right);
    }
  }
  os << "}\n";
  delete pp;
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
