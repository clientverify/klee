//===-- PrintReadExpr.cpp ------------------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

// Used for debugging implied value concretizations. (can remove this later)

#include "Common.h"
 
#include "ImpliedValue.h"
#include "Memory.h"

#include "klee/ExecutionState.h"
#include "klee/Expr.h"
#include "klee/Common.h"
#include "klee/Internal/Module/Cell.h"
#include "klee/Internal/Module/InstructionInfoTable.h"
#include "klee/Internal/Module/KInstruction.h"
#include "klee/Internal/Module/KModule.h"
#include "klee/util/Assignment.h"
#include "klee/util/ExprPPrinter.h"
#include "klee/util/ExprUtil.h"

#include <cassert>
#include <algorithm>
#include <iostream>
#include <iomanip>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>

using namespace llvm;
using namespace klee;

struct ReadExprInfo { 
  const Array *root;
  std::set<std::string> types; 
  std::set<ref<Expr> > symbolicIndexSet; 
  std::set<uint64_t> indexSet; 
  std::set<ObjectState*> objectStateSet; 
  
  void addObjectState(ObjectState* os) {
    objectStateSet.insert(os);
  }

  void addInfo(ref<ReadExpr> re, std::string &type) {
    assert(re->updates.root && "ReadExpr doesn't have root");
    root = re->updates.root;
    types.insert(type);
    if (klee::ConstantExpr *CE = dyn_cast<klee::ConstantExpr>(re->index))
      indexSet.insert(CE->getZExtValue());
    else
      symbolicIndexSet.insert(re->index);
  }

  void print(std::ostream &os) const {
    os << root->name << " ";
    std::copy(types.begin(), types.end(), 
              std::ostream_iterator<std::string>(os,", "));

    int rangeStart = 0;
    std::vector<uint64_t> indexList(indexSet.begin(),indexSet.end());
    std::sort(indexList.begin(), indexList.end());
    for (int i=1;i<=indexList.size(); ++i) {
      if (i == indexList.size() || (indexList[i] - indexList[i-1]) > 1) {
        if (rangeStart == i-1) 
          os << indexList[i-1];
        else 
          os << indexList[rangeStart] << "-" << indexList[i-1];

        if (i < indexList.size()) os << ",";
        rangeStart = i;
      }
    }
    os << "\n";
    if (symbolicIndexSet.size() > 0) {
      os << " Symbolic Index Count: " << symbolicIndexSet.size() << "\n";
    }
    if (objectStateSet.size())
      os << "ObjectStates (" << objectStateSet.size() << "):\n";
    for (std::set<ObjectState*>::iterator it=objectStateSet.begin(),
         ie=objectStateSet.end(); it!=ie; ++it) { (*it)->print(os, false);
      os << "\n";
    }
  }
};

std::ostream &operator<<(std::ostream &os, const ReadExprInfo &ri) {ri.print(os);}

typedef std::map<std::string, ReadExprInfo> ReadExprMap;
typedef std::vector<ref<ReadExpr> > ReadExprVector;

static int updateReadExprMap(ref<Expr> e, std::string type,
                             ReadExprMap &readExprMap, ObjectState *os=NULL) {
  if (!e.isNull() && !isa<klee::ConstantExpr>(e)) {
    std::vector<ref<ReadExpr> > reads;
    findReads(e, true, reads);
    for (ReadExprVector::iterator rit=reads.begin(), rie=reads.end(); 
        rit!=rie; ++rit) {
      ref<ReadExpr> re = *rit;
      if (re->updates.root) {
        readExprMap[re->updates.root->name].addInfo(re, type);
        if (os)
          readExprMap[re->updates.root->name].addObjectState(os);
      }
    }
    return reads.size();
  }
  return 0;
}

static void printAllReadExpr(std::ostream &out, ExecutionState &state) {

  unsigned count = 0;
  std::map<std::string, ReadExprInfo> reMap;

  out << "ReadExpr Begin\n";
  
  MemoryMap addressSpaceObjects = state.addressSpace.objects;
  for (MemoryMap::iterator it = addressSpaceObjects.begin(),
        ie = addressSpaceObjects.end(); it != ie; ++it) {
    ObjectState *os = it->second;
    for (unsigned i=0; i<os->size; i++)
      count += updateReadExprMap(os->read8(i), "Object", reMap, os);
  }

  for (int i=0; i<state.stack.size(); ++i) {
    StackFrame &stack = state.stack[i];
    for (int j=0; j < stack.kf->numRegisters; ++j)
      count += updateReadExprMap(stack.locals[j].value, "Stack", reMap);
  }

  ConstraintManager &cm = state.constraints;
  for (ConstraintManager::const_iterator it=cm.begin(),ie=cm.end();
       it!=ie; ++it) {
    count += updateReadExprMap(*it, "Constraints", reMap);
  }

  for (ReadExprMap::iterator it=reMap.begin(),ie=reMap.end();
       it!=ie; ++it) {
    out << it->second;
  }

  out << "ReadExpr End " << count << "\n";
}

