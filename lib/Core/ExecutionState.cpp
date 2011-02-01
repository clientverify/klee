//===-- ExecutionState.cpp ------------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "klee/ExecutionState.h"

#include "klee/Internal/Module/Cell.h"
#include "klee/Internal/Module/InstructionInfoTable.h"
#include "klee/Internal/Module/KInstruction.h"
#include "klee/Internal/Module/KModule.h"

#include "klee/Expr.h"
/* NUKLEAR KLEE begin */
#include "Common.h"
#include "CoreStats.h"
#include "CallPathManager.h"
#include "klee/Statistics.h"
#include "klee/TimerStatIncrementer.h"
#include "klee/IndependentElementSet.h"
#include <iomanip>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
/* NUKLEAR KLEE end */

#include "Memory.h"
#include "MemoryManager.h"

#include "llvm/Function.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/raw_ostream.h"

#include <iostream>
#include <iomanip>
#include <cassert>
#include <map>
#include <set>
#include <stdarg.h>

using namespace llvm;
using namespace klee;

namespace { 
  cl::opt<bool>
  DebugLogStateMerge("debug-log-state-merge");
  /* NUKLEAR KLEE begin */
  cl::opt<bool>
  NuklearFastDigest("nuklear-fast-digest");
  cl::opt<bool>
  NuklearPruneHack("nuklear-prune-hack",cl::init(false));
  cl::opt<bool>
  NuklearPrintStateBranch("nuklear-print-state-branch",cl::init(false));
  /* NUKLEAR KLEE end */
}

/***/

StackFrame::StackFrame(KInstIterator _caller, KFunction *_kf)
  : caller(_caller), kf(_kf), callPathNode(0), 
    minDistToUncoveredOnReturn(0), varargs(0) {
  locals = new Cell[kf->numRegisters];
}

StackFrame::StackFrame(const StackFrame &s) 
  : caller(s.caller),
    kf(s.kf),
    callPathNode(s.callPathNode),
    allocas(s.allocas),
    minDistToUncoveredOnReturn(s.minDistToUncoveredOnReturn),
    varargs(s.varargs) {
  locals = new Cell[s.kf->numRegisters];
  for (unsigned i=0; i<s.kf->numRegisters; i++) {
    locals[i] = s.locals[i];
	}
  // increase reference count on stack frame copy
  for (std::vector<const MemoryObject*>::iterator it = allocas.begin(),
      ie = allocas.end(); it != ie; ++it) {
    (*it)->refCount++;
  }
 }

StackFrame::~StackFrame() { 
  delete[] locals; 
}

/***/

/* NUKLEAR KLEE end */
int ExecutionState::counter = 0;
/* NUKLEAR KLEE begin */

ExecutionState::ExecutionState(KFunction *kf, MemoryManager *mem)
  : fakeState(false),
    underConstrained(false),
    depth(0),
    /* NUKLEAR KLEE begin */
    id(++counter),
    /* NUKLEAR KLEE end */
    pc(kf->instructions),
    prevPC(pc),
    queryCost(0.), 
    weight(1),
    instsSinceCovNew(0),
    coveredNew(false),
    forkDisabled(false),
    ptreeNode(0),
    memory(mem) {
  pushFrame(0, kf);
}

ExecutionState::ExecutionState(const std::vector<ref<Expr> > &assumptions) 
  : fakeState(true),
    underConstrained(false),
    /* NUKLEAR KLEE begin */
    id(++counter),
    /* NUKLEAR KLEE end */
    constraints(assumptions),
    queryCost(0.),
    ptreeNode(0) {
}

/* NUKLEAR KLEE begin */
ExecutionState::ExecutionState(const ExecutionState &es)
  : 
    fnAliases(es.fnAliases),
    fakeState(es.fakeState),
    underConstrained(es.underConstrained),
    depth(es.depth),
    id(++counter),
    nuklearSockets(es.nuklearSockets),
    pc(es.pc),
    prevPC(es.prevPC),
    stack(es.stack),
    constraints(es.constraints),
    queryCost(es.queryCost),
    weight(es.weight),
    addressSpace(es.addressSpace),
    pathOS(es.pathOS),
    symPathOS(es.symPathOS),
    instsSinceCovNew(es.instsSinceCovNew),
    coveredNew(es.coveredNew),
    forkDisabled(es.forkDisabled),
    coveredLines(es.coveredLines),
    ptreeNode(es.ptreeNode),
    memory(es.memory),
    symbolics(es.symbolics),
    shadowObjects(es.shadowObjects),
    incomingBBIndex(es.incomingBBIndex)
{ }
/* NUKLEAR KLEE end */

ExecutionState::~ExecutionState() {
  while (!stack.empty()) popFrame();
}

ExecutionState *ExecutionState::branch() {
  depth++;

  ExecutionState *falseState = new ExecutionState(*this);
	if (NuklearPrintStateBranch) 
		klee_warning("New state (%d) branched from (%d)", falseState->id, this->id);
  falseState->coveredNew = false;
  falseState->coveredLines.clear();

  weight *= .5;
  falseState->weight -= weight;

  return falseState;
}

void ExecutionState::addAlloca(const MemoryObject *mo) {
  // increase reference count on new local variable
  mo->refCount++;
  stack.back().allocas.push_back(mo);
}

void ExecutionState::pushFrame(KInstIterator caller, KFunction *kf) {
  stack.push_back(StackFrame(caller,kf));
}

void ExecutionState::popFrame() {
  StackFrame &sf = stack.back();
  for (std::vector<const MemoryObject*>::iterator it = sf.allocas.begin(), 
         ie = sf.allocas.end(); it != ie; ++it) {
    addressSpace.unbindObject(*it);
    // Decrease reference count on stack frame pop.
    // Deallocate the MemoryObject iff refCount reaches 0
    // and the memory object wasn't made symbolic (otherwise
    // the test case generation will miss references to mo's).
    if (!--(*it)->refCount && !(*it)->isMadeSymbolic) {
      //assert(memory && "MemoryManager not initialized");
      //memory->deallocate(*it);
			MemoryObject *ncmo = const_cast<MemoryObject*>(*it);
			ncmo->deallocate = true;
    }
  }
  stack.pop_back();
}

void ExecutionState::addSymbolic(const MemoryObject *mo, const Array *array) { 
  mo->isMadeSymbolic = true;
  symbolics.push_back(std::make_pair(mo, array));
}

 
///

std::string ExecutionState::getFnAlias(std::string fn) {
  std::map < std::string, std::string >::iterator it = fnAliases.find(fn);
  if (it != fnAliases.end())
    return it->second;
  else return "";
}

void ExecutionState::addFnAlias(std::string old_fn, std::string new_fn) {
  fnAliases[old_fn] = new_fn;
}

void ExecutionState::removeFnAlias(std::string fn) {
  fnAliases.erase(fn);
}

/**/

std::ostream &klee::operator<<(std::ostream &os, const MemoryMap &mm) {
  os << "{";
  MemoryMap::iterator it = mm.begin();
  MemoryMap::iterator ie = mm.end();
  if (it!=ie) {
    os << "MO" << it->first->id << ":" << it->second;
    for (++it; it!=ie; ++it)
      os << ", MO" << it->first->id << ":" << it->second;
  }
  os << "}";
  return os;
}

/* NUKLEAR KLEE begin */
void ExecutionState::print_diff(std::vector<ExecutionState*> &ev) {

  for (typeof(ev.begin()) ei=ev.begin(), ee=ev.end(); ei!=ee; ++ei) {
    ExecutionState* e = *ei;
    llvm::errs() << "\tExecutionState ID: " << e->id << "\n";

    for (std::map<int,NuklearSocket>::iterator it=e->nuklearSockets.begin(),
         ie=e->nuklearSockets.end(); it!=ie; ++it) {
      NuklearSocket *na = &it->second;
      llvm::errs() << "\tNuklearSocket: " << it->first << ", Index: " << na->index << "\n";
    }

    // Symbolics
    llvm::errs() << "\tsymbolics: [";
    for(typeof(e->symbolics.begin()) it=e->symbolics.begin(), ie=e->symbolics.end();
        it!=ie; ++it) {
      llvm::errs() << it->first->name << ", ";
    }
    llvm::errs() << "]\n";

    // StackFrames
    {
      std::vector<StackFrame>::const_iterator itA = e->stack.begin();
      while (itA!=e->stack.end()) {
        (*itA).callPathNode->print();
        llvm::errs() << "\n";
        ++itA;
      }
    }

    {
      llvm::errs() << "\tconstraints : [\n";
      std::set< ref<Expr> > constraintsSet(e->constraints.begin(), e->constraints.end());
			std::stringstream ss;
      for (std::set< ref<Expr> >::iterator it = constraintsSet.begin(), 
             ie = constraintsSet.end(); it != ie; ++it)
				ss << *it << ",\n";
			llvm::errs() << ss.str();
      llvm::errs() << "]\n";
    }
  }

  llvm::errs() << "\tMemory Diff of all ExecutionStates: \n";
  for (typeof(ev.begin()) ei=ev.begin(), ee=ev.end(); ei!=ee; ++ei) {
    ExecutionState* e = *ei;
    MemoryMap::iterator ai = e->addressSpace.objects.begin();
    MemoryMap::iterator ae = e->addressSpace.objects.end();
    for (; ai!=ae; ++ai) {
      bool missing = false;
      bool mutated = false;
      ObjectState *os = ai->second;
      std::vector<ObjectState*> osvec;
      osvec.push_back(os);

      for (typeof(ev.begin()) aei=ev.begin(), aee=ev.end(); aei!=aee; ++aei) {
        ExecutionState* aes = *aei;
				if (aes != e || ev.size() == 1) {
					MemoryMap::iterator mmi = aes->addressSpace.objects.find(ai->first);
					if (mmi != aes->addressSpace.objects.end()) {
						ObjectState *tmpos = mmi->second;
						osvec.push_back(tmpos);
						if (tmpos->compare(*os)) {
							mutated = true;
						}
					} else {
						missing = true;
						break;
					}
				}
      }

      if (mutated) {
        llvm::errs() << "\t\tmutated states: \n";
        os->print_diff(osvec, std::cerr);
        llvm::errs() << "\n";
      }
      if (missing) 
        llvm::errs() << "\t\tmissing states. \n";
    }
    break;
  }

  llvm::errs() << "-------------------------------------------------\n";
  llvm::errs() << "\tMutated Memory of all ExecutionStates: \n";
  for (typeof(ev.begin()) ei=ev.begin(), ee=ev.end(); ei!=ee; ++ei) {
    ExecutionState* e = *ei;
    llvm::errs() << "\tMemory of ExecutionState ID: " << e->id << "\n";
    MemoryMap::iterator ai = e->addressSpace.objects.begin();
    MemoryMap::iterator ae = e->addressSpace.objects.end();
    for (; ai!=ae; ++ai) {
      bool mutated = false;
      ObjectState *os = ai->second;

      for (typeof(ev.begin()) aei=ev.begin(), aee=ev.end(); aei!=aee; ++aei) {
        ExecutionState* aes = *aei;
        MemoryMap::iterator mmi = aes->addressSpace.objects.find(ai->first);
        if (mmi != aes->addressSpace.objects.end()) {
          ObjectState *tmpos = mmi->second;
          if (tmpos->compare(*os)) {
            mutated = true;
            break;
          }
        } else {
           mutated = true;
           break;
        }
      }

      if (mutated) {
        llvm::errs() << "\t\tmutated: " << ai->first->id << "\n";
        llvm::errs() << "\t\t object state A: ";
        os->print();
        llvm::errs() << "\n";
      }
    }

  }
}

void ExecutionState::print(std::vector<ExecutionState*> &ev) {

  for (typeof(ev.begin()) ei=ev.begin(), ee=ev.end(); ei!=ee; ++ei) {
    ExecutionState* e = *ei;
    llvm::errs() << "\tExecutionState ID: " << e->id << "\n";

    // Symbolics
    llvm::errs() << "\tsymbolics: [";
    for(typeof(e->symbolics.begin()) it=e->symbolics.begin(), ie=e->symbolics.end();
        it!=ie; ++it) {
      llvm::errs() << it->first->name << ", ";
    }
    llvm::errs() << "]\n";

    // StackFrames
    {
      std::vector<StackFrame>::const_iterator itA = e->stack.begin();
      while (itA!=e->stack.end()) {
        (*itA).callPathNode->print();
        llvm::errs() << "\n";
        ++itA;
      }
    }

    {
      llvm::errs() << "\tconstraints : [";
      std::set< ref<Expr> > constraintsSet(e->constraints.begin(), e->constraints.end());
			std::stringstream ss;
      for (std::set< ref<Expr> >::iterator it = constraintsSet.begin(), 
             ie = constraintsSet.end(); it != ie; ++it)
        ss << *it << ", ";
			llvm::errs() << ss.str();
      llvm::errs() << "]\n";
    }

    MemoryMap::iterator ai = e->addressSpace.objects.begin();
    MemoryMap::iterator ae = e->addressSpace.objects.end();
    for (; ai!=ae; ++ai) {
      bool mutated = false;
      ObjectState *os = ai->second;

      for (typeof(ev.begin()) aei=ev.begin(), aee=ev.end(); aei!=aee; ++aei) {
        ExecutionState* aes = *aei;
        MemoryMap::iterator mmi = aes->addressSpace.objects.find(ai->first);
        if (mmi != aes->addressSpace.objects.end()) {
          ObjectState *tmpos = mmi->second;
          if (tmpos->compare(*os)) {
            mutated = true;
            break;
          }
        } else {
           mutated = true;
           break;
        }
      }

      if (mutated) {
        llvm::errs() << "\t\tmutated: " << ai->first->id << "\n";
        llvm::errs() << "\t\t object state A: ";
        os->print();
        llvm::errs() << "\n";
      }
    }
  }
}

// (rcochran) modified version of ExecutionState::merge() that is less
// aggressive, only merges if symbolics, stack, address-space and path
// constraints are equivalent.
bool ExecutionState::nuklear_merge(const ExecutionState &b) {
  // Reasons for merge failure: 
  // PC
  if (DebugLogStateMerge)
    llvm::errs() << "-- attempting merge of A:" 
               << this << " (" << id << ")  with B:" 
               << &b << " (" << b.id << ") --\n";
  if (pc != b.pc) {
    if (DebugLogStateMerge) {
      llvm::errs() << "\t\tmerge failed: pc\n";
    }
    return false;
  }

  for (std::map<int,NuklearSocket>::iterator it=nuklearSockets.begin(),
       ie=nuklearSockets.end(); it!=ie; ++it) {
    // ugh fix this constness
    ExecutionState *es = const_cast<ExecutionState*>(&b);
    std::map<int, NuklearSocket>::iterator it2 
      = es->nuklearSockets.find(it->first);
    if (it2 != b.nuklearSockets.end()) {
      NuklearSocket *na = &it->second;
      const NuklearSocket * nb= &it2->second;
      //if (na->compare(*nb)) {
      if (na->index != nb->index) {
        if (DebugLogStateMerge)
          llvm::errs() << "\t\tmerge failed: nuklear sockets not equal "
            << na->index << " != " << nb->index << "\n";
        return false;
      }
    } else {
      if (DebugLogStateMerge)
        llvm::errs() << "\t\tmerge failed: missing nuklear sockets\n";
      return false;
    }
  }

  // XXX is it even possible for these to differ? does it matter? probably
  // implies difference in object states?
  if (symbolics!=b.symbolics) {
    if (DebugLogStateMerge) {
      llvm::errs() << "      symbolics: [";
      for(typeof(symbolics.begin()) it=symbolics.begin(), ie=symbolics.end();
          it!=ie; ++it) {
        llvm::errs() << it->first->name << ", ";
      }
      llvm::errs() << "]\n";
      llvm::errs() << " != b.symbolics: [";
      for(typeof(b.symbolics.begin()) it=b.symbolics.begin(), ie=b.symbolics.end();
          it!=ie; ++it) {
        llvm::errs() << it->first->name << ", ";
      }
      llvm::errs() << "]\n";
      llvm::errs() << "\t\tmerge failed: symbolics\n";
    }
    return false;
  } else {
    //if (DebugLogStateMerge) {
    //  llvm::errs() << "      symbolics: [";
    //  for(typeof(symbolics.begin()) it=symbolics.begin(), ie=symbolics.end();
    //      it!=ie; ++it) {
    //    llvm::errs() << it->first->name << ", ";
    //  }
    //  llvm::errs() << "]\n";
    //}
  }

  {
    std::vector<StackFrame>::const_iterator itA = stack.begin();
    std::vector<StackFrame>::const_iterator itB = b.stack.begin();
    while (itA!=stack.end() && itB!=b.stack.end()) {
      // XXX vaargs?
      if (itA->caller!=itB->caller || itA->kf!=itB->kf) {
        if (DebugLogStateMerge) {
          //llvm::errs() << "itA->caller!=itB->caller || itA->kf!=itB->kf\n";
          llvm::errs() << "\t\tmerge failed: caller on stack\n";
        }
        return false;
      }
      ++itA;
      ++itB;
    }
    if (itA!=stack.end() || itB!=b.stack.end()) {
      if (DebugLogStateMerge) {
        //llvm::errs() << "itA!=stack.end() || itB!=b.stack.end()\n";
        llvm::errs() << "\t\tmerge failed: end of stack\n";
      }
      return false;
    }
  }

  std::set< ref<Expr> > aConstraints(constraints.begin(), constraints.end());
  std::set< ref<Expr> > bConstraints(b.constraints.begin(), 
                                     b.constraints.end());

  std::set< ref<Expr> > commonConstraints, aSuffix, bSuffix;

  std::set_intersection(aConstraints.begin(), aConstraints.end(),
                        bConstraints.begin(), bConstraints.end(),
                        std::inserter(commonConstraints, commonConstraints.begin()));

  std::set_difference(aConstraints.begin(), aConstraints.end(),
                      commonConstraints.begin(), commonConstraints.end(),
                      std::inserter(aSuffix, aSuffix.end()));

  std::set_difference(bConstraints.begin(), bConstraints.end(),
                      commonConstraints.begin(), commonConstraints.end(),
                      std::inserter(bSuffix, bSuffix.end()));

  if (aSuffix.size() > 0 || bSuffix.size() > 0) {
    if (DebugLogStateMerge) {
			std::stringstream ss;
      ss << "\tconstraint prefix: [";
      for (std::set< ref<Expr> >::iterator it = commonConstraints.begin(), 
             ie = commonConstraints.end(); it != ie; ++it)
        ss << *it << ", ";
      ss << "]\n";
      
      ss << "\tA suffix: [";
      for (std::set< ref<Expr> >::iterator it = aSuffix.begin(), 
             ie = aSuffix.end(); it != ie; ++it)
        ss << *it << ", ";
      ss << "]\n";
      ss << "\tB suffix: [";
      for (std::set< ref<Expr> >::iterator it = bSuffix.begin(), 
             ie = bSuffix.end(); it != ie; ++it)
        ss << *it << ", ";
      ss << "]\n";
      ss << "\t\tmerge failed: constraints\n";
			llvm::errs() << ss.str();
    }
    return false;
  }

  // We cannot merge if addresses would resolve differently in the
  // states. This means:
  // 
  // 1. Any objects created since the branch in either object must
  // have been free'd.
  //
  // 2. We cannot have free'd any pre-existing object in one state
  // and not the other

  //if (DebugLogStateMerge) {
  //  llvm::errs() << "\tchecking object states\n";
  //  llvm::errs() << "A: " << addressSpace.objects << "\n";
  //  llvm::errs() << "B: " << b.addressSpace.objects << "\n";
  //}
  
  std::set<const MemoryObject*> mutated;

  typedef std::map<const llvm::Value*, ObjectPair > AllocSiteMap;
  typedef std::pair<const llvm::Value*, ObjectPair > AllocSitePair;
  AllocSiteMap unmatchedA;
  AllocSiteMap unmatchedB;

  //std::map<const MemoryObject*, const ObjectState*> unmatchedA;
  //std::map<const MemoryObject*, const ObjectState*> unmatchedB;

  if (addressSpace.objects.size() != b.addressSpace.objects.size()) {
    if (DebugLogStateMerge)
      llvm::errs() << "\t\tdifferent address space sizes\n";
    return false;
  }

  std::map<const MemoryObject*, const ObjectState*> addressSpaceA;
  std::map<const MemoryObject*, const ObjectState*> addressSpaceB;

  {
    MemoryMap::iterator ai = addressSpace.objects.begin();
    MemoryMap::iterator bi = b.addressSpace.objects.begin();
    MemoryMap::iterator ae = addressSpace.objects.end();
    MemoryMap::iterator be = b.addressSpace.objects.end();
    
    //for (; ai!=ae ; ++ai) {
    //  assert(ai->first && "ai->first is NULL");
    //  addressSpaceA.insert(
    //    std::pair<const MemoryObject*, const ObjectState*>(ai->first, 
    //                                                       ai->second));
    //}

    //for (; bi!=be ; ++bi)  {
    //  assert(bi->first && "bi->first is NULL");
    //  addressSpaceB.insert(
    //    std::pair<const MemoryObject*, const ObjectState*>(bi->first, 
    //                                                       bi->second));
    //}
    
    for (; ai!=ae ; ++ai) addressSpaceA.insert(*ai);
    for (; bi!=be ; ++bi) addressSpaceB.insert(*bi);
  }

  // For each object in A, find it's match in B and compare. If not found add it
  // to the unmatchedA set.
  for (std::map<const MemoryObject*, const ObjectState*>::iterator 
       ai = addressSpaceA.begin(), ae = addressSpaceA.end(); ai!=ae; ++ai) {

    std::map<const MemoryObject*, const ObjectState*>::iterator bi = 
      addressSpaceB.find(ai->first);

    if (bi != addressSpaceB.end()) {
      if (ai->second->compare(*bi->second)) {
        if (DebugLogStateMerge) {
          llvm::errs() << "\t\tmutated: " << ai->first->id << "\n";
          llvm::errs()  << "\t\t object state A: ";
          const_cast<ObjectState*>(ai->second)->print();
          llvm::errs()  << "\n\t\t object state B: ";
          const_cast<ObjectState*>(bi->second)->print();
          llvm::errs() << "\n\t\tmerge failed: mutated object state\n";
        }
        if (!ai->first->allocSite)
          llvm::errs() << "\n\t\tmerge failed: Null allocSite\n";
        return false;
      }
      addressSpaceB.erase(bi->first);

    } else {
      
      if (ai->first->allocSite) {
        assert(unmatchedA.count(ai->first->allocSite) == 0
               && "multiple allocs from same site in a single round");
        AllocSitePair a_pair(ai->first->allocSite, 
                             ObjectPair(ai->first, ai->second));
        unmatchedA.insert(a_pair);
      } else {

        if (DebugLogStateMerge) {
          llvm::errs()  << "\t\t object state A: ";
          const_cast<ObjectState*>(ai->second)->print();
        }
          llvm::errs() << "\n\t\tmerge failed: Null allocSite\n";
        return false;
      }
    }

    addressSpaceA.erase(ai->first);
  }

  //if (ai!=ae || bi!=be) {
  //  if (DebugLogStateMerge) {
  //    llvm::errs() << "\t\tmerge failed: address space size\n";
  //  }
  //  return false;
  //}

  // Build the unmatchedB set, 
  for (std::map<const MemoryObject*, const ObjectState*>::iterator 
       bi = addressSpaceB.begin(), be = addressSpaceB.end(); bi!=be; ++bi) {
    if (bi->first->allocSite) {
      assert(unmatchedB.count(bi->first->allocSite) == 0
             && "multiple allocs from same site in a single round");
      AllocSitePair b_pair(bi->first->allocSite, 
                           ObjectPair(bi->first, bi->second));
      unmatchedB.insert(b_pair);
    } else {
      if (DebugLogStateMerge) {
        llvm::errs()  << "\t\t object state B: ";
        const_cast<ObjectState*>(bi->second)->print();
      }
        llvm::errs() << "\n\t\tmerge failed: Null allocSite\n";
      return false;
    }

  }
 
  assert(unmatchedA.size() == unmatchedB.size());

  if (unmatchedA.size() || unmatchedB.size()) {

    for (AllocSiteMap::iterator asit=unmatchedA.begin(), 
                                  ie=unmatchedA.end(); asit!=ie; ++asit) {
      AllocSiteMap::iterator bsit = unmatchedB.find(asit->first);

      if (bsit != unmatchedB.end()) {
        ObjectState *os_a = const_cast<ObjectState*>(asit->second.second);
        ObjectState *os_b = const_cast<ObjectState*>(bsit->second.second);

        if (os_a->compare(*os_b)) {
          if (DebugLogStateMerge) {
            llvm::errs() << "\t\tmutated: " << asit->second.first->id << "\n";
            llvm::errs()  << "\t\t object state A: ";
            os_a->print();
            llvm::errs()  << "\n\t\t object state B: ";
            os_b->print();
            llvm::errs() << "\n\t\tmerge failed: mutated object state\n";
          }
          return false;
        }
      } else {
        if (DebugLogStateMerge) {
          llvm::errs() << "\t\tmerge failed: no allocSite match\n";
        }
        return false;
      } 
    }
  }

  //if (mutated.size() > 0) {
  //  return false;
  //}
  
  // merge stack

  //ref<Expr> inA = ConstantExpr::alloc(1, Expr::Bool);
  //ref<Expr> inB = ConstantExpr::alloc(1, Expr::Bool);
  //for (std::set< ref<Expr> >::iterator it = aSuffix.begin(), 
  //       ie = aSuffix.end(); it != ie; ++it)
  //  inA = AndExpr::create(inA, *it);
  //for (std::set< ref<Expr> >::iterator it = bSuffix.begin(), 
  //       ie = bSuffix.end(); it != ie; ++it)
  //  inB = AndExpr::create(inB, *it);

  // XXX should we have a preference as to which predicate to use?
  // it seems like it can make a difference, even though logically
  // they must contradict each other and so inA => !inB

  if (DebugLogStateMerge) {
    llvm::errs() << "\t\tmerge succeeded\n";
  }

  return true;

#if 0
  bool locals_failed = false;
  std::vector<StackFrame>::iterator itA = stack.begin();
  std::vector<StackFrame>::const_iterator itB = b.stack.begin();
  for (; itA!=stack.end(); ++itA, ++itB) {
    StackFrame &af = *itA;
    const StackFrame &bf = *itB;
    // NOTE(rcochran) \invariant af.kf->numRegisters == bf.kf->numRegisters
    // NOTE(rcochran) registers are stored in af.locals? (allocas?)
    for (unsigned i=0; i<af.kf->numRegisters; i++) {
      ref<Expr> &av = af.locals[i].value;
      const ref<Expr> &bv = bf.locals[i].value;
      
      //if (av.isNull() || bv.isNull()) {
      //  // if one is null then by implication (we are at same pc)
      //  // we cannot reuse this local, so just ignore
      //} else {
      //  av = SelectExpr::create(inA, av, bv);
      //}
 
      if (av.isNull() && bv.isNull())
          continue;

      if (av.isNull() || bv.isNull())
          continue;
     
      if (av.isNull() && !bv.isNull()) {
        llvm::errs() << "StackFrame:\n";
        af.callPathNode->print();
        llvm::errs() << "\n";
        llvm::errs() << "\tlocals are not equal: NULL != " << bv << "\n";
        llvm::errs() << "\t\tmerge failed: locals\n";
        //return false;
        locals_failed = true;
      }

      if (!av.isNull() && bv.isNull()) {
        llvm::errs() << "StackFrame:\n";
        af.callPathNode->print();
        llvm::errs() << "\n";
        llvm::errs() << "\tlocals are not equal: " << av << " != NULL\n";
        llvm::errs() << "\t\tmerge failed: locals\n";
        //return false;
        locals_failed = true;
      }

      if (av != bv) {
        {
          llvm::errs() << "StackFrame A:\n";
          af.callPathNode->print();
          llvm::errs() << "\n";
          int k = i;
          KInstruction* kinst = af.kf->instructions[i];
          while (kinst->dest != i) {
            k--;
            kinst = af.kf->instructions[k];
          }
          llvm::errs() << "Instruction A i:" << i << "\n";
          llvm::errs() << "Instruction A k:" << k << "\n";
          llvm::errs() << "Instruction A dest:" << kinst->dest << "\n";
          Instruction* inst = kinst->inst;
          llvm::errs() << "Instruction A:\n";
          llvm::errs() << *inst << "\n";
        } {
          llvm::errs() << "StackFrame B:\n";
          bf.callPathNode->print();
          llvm::errs() << "\n";
          int k = i;
          KInstruction* kinst = bf.kf->instructions[i];
          while (kinst->dest != i) {
            k--;
            kinst = af.kf->instructions[k];
          }
          llvm::errs() << "Instruction B i:" << i << "\n";
          llvm::errs() << "Instruction B k:" << k << "\n";
          llvm::errs() << "Instruction B dest:" << kinst->dest << "\n";
          Instruction* inst = kinst->inst;
          llvm::errs() << "Instruction B:\n";
          llvm::errs() << *inst << "\n";
        }

        llvm::errs() << "\tlocals are not equal: " << av << " != " << bv << "\n";
        llvm::errs() << "\t\tmerge failed: locals\n";
        //return false;
        locals_failed = true;
      }
    }
  }
  if (locals_failed) return false;

  //for (std::set<const MemoryObject*>::iterator it = mutated.begin(), 
  //       ie = mutated.end(); it != ie; ++it) {
  //  const MemoryObject *mo = *it;
  //  const ObjectState *os = addressSpace.findObject(mo);
  //  const ObjectState *otherOS = b.addressSpace.findObject(mo);
  //  assert(os && !os->readOnly && 
  //         "objects mutated but not writable in merging state");
  //  assert(otherOS);

  //  ObjectState *wos = addressSpace.getWriteable(mo, os);
  //  for (unsigned i=0; i<mo->size; i++) {
  //    ref<Expr> av = wos->read8(i);
  //    ref<Expr> bv = otherOS->read8(i);
  //    wos->write(i, SelectExpr::create(inA, av, bv));
  //  }
  //}

  //constraints = ConstraintManager();
  //for (std::set< ref<Expr> >::iterator it = commonConstraints.begin(), 
  //       ie = commonConstraints.end(); it != ie; ++it)
  //  constraints.addConstraint(*it);
  //constraints.addConstraint(OrExpr::create(inA, inB));

  if (DebugLogStateMerge) {
    llvm::errs() << "\t\tmerge succeeded\n";
  }
  return true;
#endif
}

void ExecutionState::computeDigest(int current_round) {
  //OpenSSL_add_all_digests();
  //const EVP_MD *md = EVP_sha512();

  EVP_MD_CTX mdctx;
  EVP_MD_CTX_init(&mdctx);
  //EVP_DigestInit_ex(&mdctx, EVP_sha512(), NULL);
  EVP_DigestInit_ex(&mdctx, EVP_md5(), NULL);
  //memset(md_value, 0, EVP_MAX_MD_SIZE);

  {
    KInstruction* kit = &(*pc);
    EVP_DigestUpdate(&mdctx, kit, sizeof(*kit));
  }

  for (std::map<int,NuklearSocket>::iterator it=nuklearSockets.begin(),
       ie=nuklearSockets.end(); it!=ie; ++it) {
    int id = it->first;
    NuklearSocket *ns = &it->second;
    EVP_DigestUpdate(&mdctx, &id, sizeof(id));
    EVP_DigestUpdate(&mdctx, ns, sizeof(*ns));
  }
  for(typeof(symbolics.begin()) it=symbolics.begin(), 
        ie=symbolics.end(); it!=ie; ++it) {
    const MemoryObject* mo = it->first;
    const Array* array = it->second;
    EVP_DigestUpdate(&mdctx, &mo, sizeof(mo));
    EVP_DigestUpdate(&mdctx, &array, sizeof(array));
  }

  std::vector<StackFrame>::const_iterator it = stack.begin(), ie = stack.end();
  for (; ie!=it; ++it) {
    //KInstruction* kit = &(*it->caller);
    //KFunction* kf = &(*it->kf);
    ////Instruction* inst = NULL;
    ////if (kit) inst = kit->inst;
    ////Function *func = kf->function;
    
    //if (kit) EVP_DigestUpdate(&mdctx, kit, sizeof(*kit));
    //if (kf) EVP_DigestUpdate(&mdctx, kf, sizeof(*kf));
    EVP_DigestUpdate(&mdctx, &it->caller, sizeof(it->caller));
    EVP_DigestUpdate(&mdctx, &it->kf, sizeof(it->kf));
    
    //if (inst) EVP_DigestUpdate(&mdctx, &inst, sizeof(inst));
    //EVP_DigestUpdate(&mdctx, &func, sizeof(func));
    //int num_reg = it->kf->numRegisters;
    //EVP_DigestUpdate(&mdctx, &num_reg, sizeof(num_reg));

    //std::stringstream ss;
    //if (it->locals) {
    //  for (int i=0; i<it->kf->numRegisters; i++) {
    //    if (!it->locals[i].value.isNull()) {
    //      ss << it->locals[i].value;
    //    }
    //  }
    //}
    //EVP_DigestUpdate(&mdctx, ss.str().c_str(), ss.str().size());
  }

  //{
  //  std::stringstream ss;
  //  for (ConstraintManager::const_iterator it = constraints.begin(), 
  //         ie = constraints.end(); it != ie; ++it) {
  //    ss << *it;
  //  }
  //  EVP_DigestUpdate(&mdctx, ss.str().c_str(), ss.str().size());
  //}
  {
    std::stringstream ss;
    for (ConstraintManager::const_iterator it = constraints.begin(), 
           ie = constraints.end(); it != ie; ++it) {
      //if (NuklearFastDigest) {
        ref<Expr> e = *it;
        unsigned hash = e->hash();
        EVP_DigestUpdate(&mdctx, &hash, sizeof(hash));
      //} else {
      //  ss << *it;
      //}
    }
    //if (!NuklearFastDigest && ss.str().size()) {
    //  EVP_DigestUpdate(&mdctx, ss.str().c_str(), ss.str().size());
    //}
  }

  // Scan the entire address space
  MemoryMap::iterator ai = addressSpace.objects.begin();
  MemoryMap::iterator ae = addressSpace.objects.end();
  for (; ae != ai;  ++ai) {
    const MemoryObject *mo = ai->first;
    EVP_DigestUpdate(&mdctx, mo, sizeof(*mo));
    ObjectState *os = ai->second;
    //std::stringstream ss;
    //os->print(ss);
    //EVP_DigestUpdate(&mdctx, ss.str().c_str(), ss.str().size());
    os->computeDigest(&mdctx, current_round);

  }

  // Scan the entire address space
  
  //{
  //  std::stringstream ss;
  //  MemoryMap::iterator ai = addressSpace.objects.begin();
  //  MemoryMap::iterator ae = addressSpace.objects.end();
  //  for (; ae != ai;  ++ai) {
  //    const MemoryObject *mo = ai->first;
  //    ObjectState *os = ai->second;
  //    std::stringstream ss;

  //    EVP_DigestUpdate(&mdctx, mo, sizeof(*mo));
  //    for (unsigned i=0; i<os->size; i++) {
  //      if(os->isByteConcrete(i)) {
  //        uint8_t c = os->getConcreteStore(i);
  //        EVP_DigestUpdate(&mdctx, &c, sizeof(c));
  //      } else {
  //        ss << os->read8(i);
  //      }
  //    }
  //  }
  //  EVP_DigestUpdate(&mdctx, ss.str().c_str(), ss.str().size());
  //}

#if 0
  unsigned addressSpace_size = addressSpace.objects.size();
  EVP_DigestUpdate(&mdctx, &addressSpace_size, sizeof(addressSpace_size));

  std::map<const Value*, const ObjectState*> allocMap;
  {
    MemoryMap::iterator ai = addressSpace.objects.begin();
    MemoryMap::iterator ae = addressSpace.objects.end();
    std::stringstream ss;
    for (; ae != ai;  ++ai) {
      const MemoryObject *mo = ai->first;
      ObjectState *os = const_cast<ObjectState*>(&(*ai->second));
      const llvm::Value *allocSite = ai->first->allocSite;
      if (!allocSite || allocMap.count(allocSite)) {
        os->computeDigest(&mdctx);
        EVP_DigestUpdate(&mdctx, mo, sizeof(*mo));
        //for (unsigned i=0; i<os->size; i++) {
        //  if(os->isByteConcrete(i)) {
        //    uint8_t c = os->getConcreteStore(i);
        //    EVP_DigestUpdate(&mdctx, &c, sizeof(c));
        //  } else {
        //    if (NuklearFastDigest) {
        //      ref<Expr> e = os->read8(i);
        //      unsigned hash = e->hash();
        //      EVP_DigestUpdate(&mdctx, &hash, sizeof(hash));
        //    } else {
        //      ss << os->read8(i);
        //    }
        //  }
        //}
      } else {
        allocMap[ai->first->allocSite] = os;
      }
    }
    //if (ss.str().size()) 
    //  EVP_DigestUpdate(&mdctx, ss.str().c_str(), ss.str().size());
  }
  {
    std::map<const Value*, const ObjectState*>::iterator ami;
    std::map<const Value*, const ObjectState*>::iterator ame;
    std::stringstream ss;
    for (; ame != ami;  ++ami) {
      const llvm::Value *allocSite = ami->first;
      ObjectState *os = const_cast<ObjectState*>(ami->second);
      EVP_DigestUpdate(&mdctx, &allocSite, sizeof(allocSite));
      os->computeDigest(&mdctx);
      //for (unsigned i=0; i<os->size; i++) {
      //  if(os->isByteConcrete(i)) {
      //    uint8_t c = os->getConcreteStore(i);
      //    EVP_DigestUpdate(&mdctx, &c, sizeof(c));
      //  } else {
      //    if (NuklearFastDigest) {
      //      ref<Expr> e = os->read8(i);
      //      unsigned hash = e->hash();
      //      EVP_DigestUpdate(&mdctx, &hash, sizeof(hash));
      //    } else {
      //      ss << os->read8(i);
      //    }
      //  }

      //}
    }
    //if (ss.str().size()) 
    //  EVP_DigestUpdate(&mdctx, ss.str().c_str(), ss.str().size());
  }
#endif

  EVP_DigestFinal_ex(&mdctx, digest.value, &digest.len);
  EVP_MD_CTX_cleanup(&mdctx);
}

bool ExecutionState::prune() {
  TimerStatIncrementer pruneTimer(stats::nuklearPruneTime);
  // Constraint pruning: TODO Description...
  // TODO Assumptions...
  // TODO Early exit if pruning not possible?...

    // TODO: make this a global change? 
    // Convert the member ExecutionState member variable: 
    //    vector<pair<const MemoryObject*, const Array*> symbolics to
    //    map<const MemoryObject*, const Array*> symbolicsMap
  std::map< const MemoryObject*, const Array* > symbolicsMap;
  std::vector<ref<Expr> >localExprs;
  {
    TimerStatIncrementer pruneSubTimer(stats::nuklearPruneTime_stack);
    for (unsigned i = 0; i != symbolics.size(); ++i)
      symbolicsMap[symbolics[i].first] = symbolics[i].second;

    // Build a ConcatExpr, will potentially be very large, but it will only be
    // used to extract the symbolic reads from the local variables in the stack
    // frame. 
    {
      std::vector<StackFrame>::const_iterator it = stack.begin();
      std::vector<StackFrame>::const_iterator ie = stack.end();
      for (; ie!=it; ++it) {
        if (it->locals) {
          //llvm::errs() << " NumRegisters: " << it->kf->numRegisters << "\n";
          for (int i=0; i<it->kf->numRegisters; i++) {
            //ref<Expr> &v = it->locals[i].value;
            //if (!v.isNull()) {
            //  if (!isa<ConstantExpr>(v)) {
            //    localExprs.push_back(v);
            //  }
            //}
            if (!it->locals[i].value.isNull()) {
              if (!isa<ConstantExpr>(it->locals[i].value)) {
                localExprs.push_back(it->locals[i].value);
                //llvm::errs() << "prune: " << it->kf->function->getNameStr() 
                //  << "() local: " << it->locals[i].value << "\n";
              }
            }

          }
        }
      }
    }
  }
   
  IndependentElementSet memorySet;
  {
    TimerStatIncrementer pruneSubTimer(stats::nuklearPruneTime_addressSpace);
    //for (std::vector< ref<Expr> >::iterator
    //       it = localExprs.begin(), ie = localExprs.end(); it != ie; ++it)
    //  memorySet.add(IndependentElementSet(*it));

    // Scan the entire address space
    MemoryMap::iterator ai = addressSpace.objects.begin();
    MemoryMap::iterator ae = addressSpace.objects.end();
    for (; ae != ai;  ++ai) {
      ObjectState *os = ai->second;
      const MemoryObject *mo = ai->first;
      
      std::map<const MemoryObject*, const Array*>::iterator symit 
        = symbolicsMap.find(mo);

      if (symit == symbolicsMap.end()) {
        if (os->hasKnownSymbolics() || os->hasFlushMask()) {
          std::vector<ref<Expr> > symbolicBytes;
          for (int i=0; i<os->size; i++) {
            ref<Expr> expr = os->read8(i);
            if (!expr.isNull() && !isa<ConstantExpr>(expr)) {
              symbolicBytes.push_back(expr);
            }
          }
          //llvm::errs() << "prune: " << mo->name << " is not in symbolics.\n";
          //llvm::errs() << "prune: " << mo->name << " has symbolic bytes\n";
          //os->print();
          for (std::vector< ref<Expr> >::iterator it = symbolicBytes.begin(), 
               ie = symbolicBytes.end(); it != ie; ++it) {
            memorySet.add(IndependentElementSet(*it));
          }
        } else {
          //llvm::errs() << "prune: " << mo->name << " is not in symbolics(2).\n";
        }
      } else {
        //llvm::errs() << "prune: " << mo->name << " is in symbolics.\n";
        //os->print();
        memorySet.addWholeObject(symit->second);
      }
      //memorySet.addWholeObject(symit->second);
    }
  }

  {
    TimerStatIncrementer pruneSubTimer(stats::nuklearPruneTime_symprune);
    std::vector< std::pair<const MemoryObject*, const Array*> > symbolicsPruned;
    for (std::map<const MemoryObject*, const Array*>::iterator 
         symi = symbolicsMap.begin(), syme=symbolicsMap.end(); syme != symi; ++symi) {
      IndependentElementSet symset;
      symset.addWholeObject(symi->second);
      if (memorySet.intersects(symset)) {
        symbolicsPruned.push_back(
          std::pair<const MemoryObject*, const Array*>(symi->first, symi->second));
      }
    }
    //llvm::errs() << "Pruned " << (int)symbolics.size() - (int)symbolicsPruned.size()
    //  << " symbolics\n";
    symbolics.swap(symbolicsPruned);
  }

  std::vector< std::pair<ref<Expr>, IndependentElementSet> > worklist;
  ConstraintManager prunedConstraints;
  {
    TimerStatIncrementer pruneSubTimer(stats::nuklearPruneTime_constraintprune);
    for (ConstraintManager::const_iterator it = constraints.begin(), 
           ie = constraints.end(); it != ie; ++it)
      worklist.push_back(std::make_pair(*it, IndependentElementSet(*it)));

    bool done = false;
    do {
      done = true;
      std::vector< std::pair<ref<Expr>, IndependentElementSet> > newWorklist;
      for (std::vector< std::pair<ref<Expr>, IndependentElementSet> >::iterator
             it = worklist.begin(), ie = worklist.end(); it != ie; ++it) {
        if (it->second.intersects(memorySet)) {
          // FIXME: HACK! turn on to fix chaining of symbolics due to calls to
          // __time() in xpilot!
          if (NuklearPruneHack == false) {
            if (memorySet.add(it->second)) {
              done = false;
            }
          }
          prunedConstraints.addConstraint(it->first);
        } else {
          newWorklist.push_back(*it);
        }
      }
      worklist.swap(newWorklist);
    } while (!done);
  }

  if (worklist.size() > 0) {
    //for (std::vector< std::pair<ref<Expr>, IndependentElementSet> >::iterator
    //       it = worklist.begin(), ie = worklist.end(); it != ie; ++it) {
    //  llvm::errs() << "Pruned: " << it->first << "\n";
    //}
    ++stats::nuklearPrunes;
    constraints = prunedConstraints;
    return true;
  }
  return false;
}

bool ExecutionState::prune_hack(std::set<std::string> current_symbolic_names) {
  TimerStatIncrementer pruneTimer(stats::nuklearPruneTime);
  // Constraint pruning: TODO Description...
  // TODO Assumptions...
  // TODO Early exit if pruning not possible?...

    // TODO: make this a global change? 
    // Convert the member ExecutionState member variable: 
    //    vector<pair<const MemoryObject*, const Array*> symbolics to
    //    map<const MemoryObject*, const Array*> symbolicsMap
  std::map< const MemoryObject*, const Array* > symbolicsMap;
  std::vector<ref<Expr> >localExprs;
  {
    TimerStatIncrementer pruneSubTimer(stats::nuklearPruneTime_stack);
    for (unsigned i = 0; i != symbolics.size(); ++i)
      symbolicsMap[symbolics[i].first] = symbolics[i].second;

    // Build a ConcatExpr, will potentially be very large, but it will only be
    // used to extract the symbolic reads from the local variables in the stack
    // frame. 
    {
      std::vector<StackFrame>::const_iterator it = stack.begin();
      std::vector<StackFrame>::const_iterator ie = stack.end();
      for (; ie!=it; ++it) {
        if (it->locals) {
          //llvm::errs() << " NumRegisters: " << it->kf->numRegisters << "\n";
          for (int i=0; i<it->kf->numRegisters; i++) {
            //ref<Expr> &v = it->locals[i].value;
            //if (!v.isNull()) {
            //  if (!isa<ConstantExpr>(v)) {
            //    localExprs.push_back(v);
            //  }
            //}
            if (!it->locals[i].value.isNull()) {
              if (!isa<ConstantExpr>(it->locals[i].value)) {
                localExprs.push_back(it->locals[i].value);
                //llvm::errs() << "prune: " << it->kf->function->getNameStr() 
                //  << "() local: " << it->locals[i].value << "\n";
              }
            }

          }
        }
      }
    }
  }
   
  IndependentElementSet memorySet;
  {
    TimerStatIncrementer pruneSubTimer(stats::nuklearPruneTime_addressSpace);
    //for (std::vector< ref<Expr> >::iterator
    //       it = localExprs.begin(), ie = localExprs.end(); it != ie; ++it)
    //  memorySet.add(IndependentElementSet(*it));

    // Scan the entire address space
    MemoryMap::iterator ai = addressSpace.objects.begin();
    MemoryMap::iterator ae = addressSpace.objects.end();
    for (; ae != ai;  ++ai) {
      ObjectState *os = ai->second;
      const MemoryObject *mo = ai->first;
      
      std::map<const MemoryObject*, const Array*>::iterator symit 
        = symbolicsMap.find(mo);

      if (symit == symbolicsMap.end()) {
        if (os->hasKnownSymbolics() || os->hasFlushMask()) {
          std::vector<ref<Expr> > symbolicBytes;
          for (int i=0; i<os->size; i++) {
            ref<Expr> expr = os->read8(i);
            if (!expr.isNull() && !isa<ConstantExpr>(expr)) {
              symbolicBytes.push_back(expr);
            }
          }
          //llvm::errs() << "prune: " << mo->name << " is not in symbolics.\n";
          //llvm::errs() << "prune: " << mo->name << " has symbolic bytes\n";
          //os->print();
          for (std::vector< ref<Expr> >::iterator it = symbolicBytes.begin(), 
               ie = symbolicBytes.end(); it != ie; ++it) {
            memorySet.add(IndependentElementSet(*it));
          }
        } else {
          //llvm::errs() << "prune: " << mo->name << " is not in symbolics(2).\n";
        }
      } else {
        //llvm::errs() << "prune: " << mo->name << " is in symbolics.\n";
        //os->print();
        memorySet.addWholeObject(symit->second);
      }
      //memorySet.addWholeObject(symit->second);
    }
  }

  {
    TimerStatIncrementer pruneSubTimer(stats::nuklearPruneTime_symprune);
    std::vector< std::pair<const MemoryObject*, const Array*> > symbolicsPruned;
    for (std::map<const MemoryObject*, const Array*>::iterator 
         symi = symbolicsMap.begin(), syme=symbolicsMap.end(); syme != symi; ++symi) {
      IndependentElementSet symset;
      symset.addWholeObject(symi->second);
      if (memorySet.intersects(symset)) {
        symbolicsPruned.push_back(
          std::pair<const MemoryObject*, const Array*>(symi->first, symi->second));
      }
    }
    //llvm::errs() << "Pruned " << (int)symbolics.size() - (int)symbolicsPruned.size()
    //  << " symbolics\n";
    symbolics.swap(symbolicsPruned);
  }
	/*
  IndependentElementSet(ref<Expr> e) {
    for (unsigned i = 0; i != reads.size(); ++i) {
      ReadExpr *re = reads[i].get();
      const Array *array = re->updates.root;
      
      // Reads of a constant array don't alias.
      if (re->updates.root->isConstantArray() &&
          !re->updates.head)
        continue;

      if (!wholeObjects.count(array)) {
        if (ConstantExpr *CE = dyn_cast<ConstantExpr>(re->index)) {
          DenseSet<unsigned> &dis = elements[array];
          dis.add((unsigned) CE->getZExtValue(32));
        } else {
          elements_ty::iterator it2 = elements.find(array);
          if (it2!=elements.end())
            elements.erase(it2);
          wholeObjects.insert(array);
        }
      }
    }
  }
	*/

  std::vector< std::pair<ref<Expr>, IndependentElementSet> > worklist;
  ConstraintManager prunedConstraints;
  {
    TimerStatIncrementer pruneSubTimer(stats::nuklearPruneTime_constraintprune);
    for (ConstraintManager::const_iterator it = constraints.begin(), 
           ie = constraints.end(); it != ie; ++it)
		{
			bool add_to_worklist=false;
			std::vector< ref<ReadExpr> > reads;
			findReads(*it, /* visitUpdates= */ true, reads);
			for (unsigned i = 0; i != reads.size(); ++i) {
				ReadExpr *re = reads[i].get();
				const Array *array = re->updates.root;

				std::set<std::string>::iterator current_symbolic_names_it
					= current_symbolic_names.find(std::string(array->name));
				if (current_symbolic_names_it != current_symbolic_names.end()) {
					add_to_worklist=true;
					continue;
				}
			}

			if (add_to_worklist)
				worklist.push_back(std::make_pair(*it, IndependentElementSet(*it)));
		}

    bool done = false;
    do {
      done = true;
      std::vector< std::pair<ref<Expr>, IndependentElementSet> > newWorklist;
      for (std::vector< std::pair<ref<Expr>, IndependentElementSet> >::iterator
             it = worklist.begin(), ie = worklist.end(); it != ie; ++it) {
        if (it->second.intersects(memorySet)) {
          // FIXME: HACK! turn on to fix chaining of symbolics due to calls to
          // __time() in xpilot!
          if (NuklearPruneHack == false) {
            if (memorySet.add(it->second)) {
              done = false;
            }
          }
          prunedConstraints.addConstraint(it->first);
        } else {
          newWorklist.push_back(*it);
        }
      }
      worklist.swap(newWorklist);
    } while (!done);
  }

  if (worklist.size() > 0) {
    //for (std::vector< std::pair<ref<Expr>, IndependentElementSet> >::iterator
    //       it = worklist.begin(), ie = worklist.end(); it != ie; ++it) {
    //  llvm::errs() << "Pruned: " << it->first << "\n";
    //}
    ++stats::nuklearPrunes;
    constraints = prunedConstraints;
    return true;
  }
  return false;
}
/* NUKLEAR KLEE end*/

bool ExecutionState::merge(const ExecutionState &b) {
  if (DebugLogStateMerge)
    std::cerr << "-- attempting merge of A:" 
               << this << " with B:" << &b << "--\n";
  if (pc != b.pc)
    return false;

  // XXX is it even possible for these to differ? does it matter? probably
  // implies difference in object states?
  if (symbolics!=b.symbolics)
    return false;

  {
    std::vector<StackFrame>::const_iterator itA = stack.begin();
    std::vector<StackFrame>::const_iterator itB = b.stack.begin();
    while (itA!=stack.end() && itB!=b.stack.end()) {
      // XXX vaargs?
      if (itA->caller!=itB->caller || itA->kf!=itB->kf)
        return false;
      ++itA;
      ++itB;
    }
    if (itA!=stack.end() || itB!=b.stack.end())
      return false;
  }

  std::set< ref<Expr> > aConstraints(constraints.begin(), constraints.end());
  std::set< ref<Expr> > bConstraints(b.constraints.begin(), 
                                     b.constraints.end());
  std::set< ref<Expr> > commonConstraints, aSuffix, bSuffix;
  std::set_intersection(aConstraints.begin(), aConstraints.end(),
                        bConstraints.begin(), bConstraints.end(),
                        std::inserter(commonConstraints, commonConstraints.begin()));
  std::set_difference(aConstraints.begin(), aConstraints.end(),
                      commonConstraints.begin(), commonConstraints.end(),
                      std::inserter(aSuffix, aSuffix.end()));
  std::set_difference(bConstraints.begin(), bConstraints.end(),
                      commonConstraints.begin(), commonConstraints.end(),
                      std::inserter(bSuffix, bSuffix.end()));
  if (DebugLogStateMerge) {
    std::cerr << "\tconstraint prefix: [";
    for (std::set< ref<Expr> >::iterator it = commonConstraints.begin(), 
           ie = commonConstraints.end(); it != ie; ++it)
      std::cerr << *it << ", ";
    std::cerr << "]\n";
    std::cerr << "\tA suffix: [";
    for (std::set< ref<Expr> >::iterator it = aSuffix.begin(), 
           ie = aSuffix.end(); it != ie; ++it)
      std::cerr << *it << ", ";
    std::cerr << "]\n";
    std::cerr << "\tB suffix: [";
    for (std::set< ref<Expr> >::iterator it = bSuffix.begin(), 
           ie = bSuffix.end(); it != ie; ++it)
      std::cerr << *it << ", ";
    std::cerr << "]\n";
  }

  // We cannot merge if addresses would resolve differently in the
  // states. This means:
  // 
  // 1. Any objects created since the branch in either object must
  // have been free'd.
  //
  // 2. We cannot have free'd any pre-existing object in one state
  // and not the other

  if (DebugLogStateMerge) {
    std::cerr << "\tchecking object states\n";
    std::cerr << "A: " << addressSpace.objects << "\n";
    std::cerr << "B: " << b.addressSpace.objects << "\n";
  }
    
  std::set<const MemoryObject*> mutated;
  MemoryMap::iterator ai = addressSpace.objects.begin();
  MemoryMap::iterator bi = b.addressSpace.objects.begin();
  MemoryMap::iterator ae = addressSpace.objects.end();
  MemoryMap::iterator be = b.addressSpace.objects.end();
  for (; ai!=ae && bi!=be; ++ai, ++bi) {
    if (ai->first != bi->first) {
      if (DebugLogStateMerge) {
        if (ai->first < bi->first) {
          std::cerr << "\t\tB misses binding for: " << ai->first->id << "\n";
        } else {
          std::cerr << "\t\tA misses binding for: " << bi->first->id << "\n";
        }
      }
      return false;
    }
    if (ai->second != bi->second) {
      if (DebugLogStateMerge)
        std::cerr << "\t\tmutated: " << ai->first->id << "\n";
      mutated.insert(ai->first);
    }
  }
  if (ai!=ae || bi!=be) {
    if (DebugLogStateMerge)
      std::cerr << "\t\tmappings differ\n";
    return false;
  }
  
  // merge stack

  ref<Expr> inA = ConstantExpr::alloc(1, Expr::Bool);
  ref<Expr> inB = ConstantExpr::alloc(1, Expr::Bool);
  for (std::set< ref<Expr> >::iterator it = aSuffix.begin(), 
         ie = aSuffix.end(); it != ie; ++it)
    inA = AndExpr::create(inA, *it);
  for (std::set< ref<Expr> >::iterator it = bSuffix.begin(), 
         ie = bSuffix.end(); it != ie; ++it)
    inB = AndExpr::create(inB, *it);

  // XXX should we have a preference as to which predicate to use?
  // it seems like it can make a difference, even though logically
  // they must contradict each other and so inA => !inB

  std::vector<StackFrame>::iterator itA = stack.begin();
  std::vector<StackFrame>::const_iterator itB = b.stack.begin();
  for (; itA!=stack.end(); ++itA, ++itB) {
    StackFrame &af = *itA;
    const StackFrame &bf = *itB;
    for (unsigned i=0; i<af.kf->numRegisters; i++) {
      ref<Expr> &av = af.locals[i].value;
      const ref<Expr> &bv = bf.locals[i].value;
      if (av.isNull() || bv.isNull()) {
        // if one is null then by implication (we are at same pc)
        // we cannot reuse this local, so just ignore
      } else {
        av = SelectExpr::create(inA, av, bv);
      }
    }
  }

  for (std::set<const MemoryObject*>::iterator it = mutated.begin(), 
         ie = mutated.end(); it != ie; ++it) {
    const MemoryObject *mo = *it;
    const ObjectState *os = addressSpace.findObject(mo);
    const ObjectState *otherOS = b.addressSpace.findObject(mo);
    assert(os && !os->readOnly && 
           "objects mutated but not writable in merging state");
    assert(otherOS);

    ObjectState *wos = addressSpace.getWriteable(mo, os);
    for (unsigned i=0; i<mo->size; i++) {
      ref<Expr> av = wos->read8(i);
      ref<Expr> bv = otherOS->read8(i);
      wos->write(i, SelectExpr::create(inA, av, bv));
    }
  }

  constraints = ConstraintManager();
  for (std::set< ref<Expr> >::iterator it = commonConstraints.begin(), 
         ie = commonConstraints.end(); it != ie; ++it)
    constraints.addConstraint(*it);
  constraints.addConstraint(OrExpr::create(inA, inB));

  return true;
}

void ExecutionState::dumpStack(std::ostream &out) const {
  unsigned idx = 0;
  const KInstruction *target = prevPC;
  for (ExecutionState::stack_ty::const_reverse_iterator
         it = stack.rbegin(), ie = stack.rend();
       it != ie; ++it) {
    const StackFrame &sf = *it;
    Function *f = sf.kf->function;
    const InstructionInfo &ii = *target->info;
    out << "\t#" << idx++ 
        << " " << std::setw(8) << std::setfill('0') << ii.assemblyLine
        << " in " << f->getNameStr() << " (";
    // Yawn, we could go up and print varargs if we wanted to.
    unsigned index = 0;
    for (Function::arg_iterator ai = f->arg_begin(), ae = f->arg_end();
         ai != ae; ++ai) {
      if (ai!=f->arg_begin()) out << ", ";

      out << ai->getNameStr();
      // XXX should go through function
      ref<Expr> value = sf.locals[sf.kf->getArgRegister(index++)].value; 
      if (isa<ConstantExpr>(value))
        out << "=" << value;
    }
    out << ")";
    if (ii.file != "")
      out << " at " << ii.file << ":" << ii.line;
    out << "\n";
    target = sf.caller;
  }
}
