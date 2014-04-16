//===-- ParallelSearcher.cpp ----------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "Common.h"

#include "Searcher.h"

#include "CoreStats.h"
#include "Executor.h"
#include "PTree.h"
#include "StatsTracker.h"

#include "klee/ExecutionState.h"
#include "klee/Statistics.h"
#include "klee/Internal/Module/InstructionInfoTable.h"
#include "klee/Internal/Module/KInstruction.h"
#include "klee/Internal/Module/KModule.h"
#include "klee/Internal/ADT/DiscretePDF.h"
#include "klee/Internal/ADT/RNG.h"
#include "klee/Internal/Support/ModuleUtil.h"
#include "klee/Internal/System/Time.h"
#if LLVM_VERSION_CODE >= LLVM_VERSION(3, 3)
#include "llvm/IR/Constants.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#else
#include "llvm/Constants.h"
#include "llvm/Instructions.h"
#include "llvm/Module.h"
#endif
#include "llvm/Support/CallSite.h"
#include "llvm/Support/CFG.h"
#include "llvm/Support/CommandLine.h"

#include <cassert>
#include <fstream>
#include <climits>

///

using namespace klee;
using namespace llvm;

namespace klee {
  extern ThreadSpecificPointer<RNG>::type theRNG;
}

ParallelDFSSearcher::ParallelDFSSearcher() {
  queue = new LockFreeStack<ExecutionState*>::type(2048);
  queueSize = 0;
  removedStatesCount = 0;
}

bool ParallelDFSSearcher::checkStateRemoved(ExecutionState* state) {
  if (removedStatesCount > 0) {
    SpinLockGuard guard(removedStatesLock);
    if (removedStatesSet.count(state)) {
      --removedStatesCount;
      removedStatesSet.erase(state);
      return true;
    }
  }
  return false;
}

ExecutionState &ParallelDFSSearcher::selectState() {
  ExecutionState* state = NULL;
  while (!queue->pop(state) || checkStateRemoved(state))
     ;

  --queueSize;
  return *state;
}

ExecutionState* ParallelDFSSearcher::trySelectState() {
  ExecutionState* state = NULL;
  while (queueSize > 0 && (!queue->pop(state) || checkStateRemoved(state)))
     ;

  if (state != NULL) 
    --queueSize;
  return state;
}

void ParallelDFSSearcher::update(ExecutionState *current,
                            const std::set<ExecutionState*> &addedStates,
                            const std::set<ExecutionState*> &removedStates) {
  if (updateAndTrySelectState(current, addedStates, removedStates) && current) {
    while (!queue->push(current))
      ;

    ++queueSize;
  }
}

ExecutionState* ParallelDFSSearcher::updateAndTrySelectState(ExecutionState *current,
                            const std::set<ExecutionState*> &addedStates,
                            const std::set<ExecutionState*> &removedStates) {
  if (addedStates.size()) {
    for (std::set<ExecutionState*>::const_iterator it = addedStates.begin(),
          ie = addedStates.end(); it != ie; ++it) {
      ExecutionState *es = *it;

      while (!queue->push(es))
          ;

      ++queueSize;
    }
  }

  bool isCurrentRemoved = false;
  if (removedStates.size()) {
    for (std::set<ExecutionState*>::const_iterator it = removedStates.begin(),
          ie = removedStates.end(); it != ie; ++it) {
      if (*it != current) {
        {
          SpinLockGuard guard(removedStatesLock);
          removedStatesSet.insert(*it);
        }
        ++removedStatesCount;
        --queueSize;
      } else {
        isCurrentRemoved = true;
      }
    }
  }

  if (isCurrentRemoved)
    return NULL;

  return current;
}

bool ParallelDFSSearcher::empty() {
  return queueSize == 0;
}

void ParallelDFSSearcher::printName(std::ostream &os) {
  os << "ParallelDFSSearcher\n";
}

///

ParallelSearcher::ParallelSearcher(Searcher *internalSearcher) 
  : searcher(internalSearcher) {
}

ExecutionState &ParallelSearcher::selectState() {
  RecursiveLockGuard guard(lock);
  ExecutionState& state = searcher->selectState();
  searcher->removeState(&state);
  return state;
}

void ParallelSearcher::update(ExecutionState *current,
                            const std::set<ExecutionState*> &addedStates,
                            const std::set<ExecutionState*> &removedStates) {
  RecursiveLockGuard guard(lock);

  if (current)
    searcher->addState(current);

  searcher->update(current, addedStates, removedStates);
}

bool ParallelSearcher::empty() {
  RecursiveLockGuard guard(lock);
  return searcher->empty();
}

void ParallelSearcher::printName(std::ostream &os) {
  os << "<ParallelSearcher>\n";
  searcher->printName(os);
  os << "</ParallelSearcher>\n";
}

ExecutionState* ParallelSearcher::trySelectState() {
  RecursiveLockGuard guard(lock);
  if (!empty()) {
    return &(selectState());
  }
  return NULL;
}

std::set<ExecutionState*> ParallelSearcher::states() {
  std::stack<ExecutionState*> stack;
  std::set<ExecutionState*> states;

  RecursiveLockGuard guard(lock);

  // Remove states from searcher
  while (ExecutionState* es = trySelectState()) {
    stack.push(es);
    states.insert(es);
  }

  // Add them back into searcher
  std::set<ExecutionState*> tmp;
  while (!stack.empty()) {
    tmp.insert(stack.top());
    stack.pop();
    update(0, tmp, std::set<ExecutionState*>());
    tmp.clear();
  }
  return states;
}

///

// ParallelWeightedRandomSearcher is currently slow in parallel because
// 1) we don't attempt to pin states to cores, so there isn't cache
// coherency between execution of states and 2) we repeatedly query the
// DiscretePDF until we get a state that isn't already active. One solution
// would be to add a new DiscretePDF::choose function will check the locked
// status and return an already locked state.

ParallelWeightedRandomSearcher::ParallelWeightedRandomSearcher(Executor &_executor,
                                               WeightType _type) 
  : executor(_executor),
    states(new DiscretePDF<ExecutionState*>()),
    type(_type) {
  switch(type) {
  case Depth: 
    updateWeights = false;
    break;
  case InstCount:
  case CPInstCount:
  case QueryCost:
  case MinDistToUncovered:
  case CoveringNew:
    updateWeights = true;
    break;
  default:
    assert(0 && "invalid weight type");
  }
  stateCount = 0;
  activeStateCount = 0;
}

ParallelWeightedRandomSearcher::~ParallelWeightedRandomSearcher() {
  delete states;
}

ExecutionState &ParallelWeightedRandomSearcher::selectState() {
  ExecutionState* es = NULL;
  SharedLock lock(statesMutex);
  while (es == NULL || !es->lock.try_lock()) {
    es = states->choose(theRNG->getDoubleL());
  }
  ++activeStateCount;
  return *es;
}

ExecutionState* ParallelWeightedRandomSearcher::trySelectState() {
  ExecutionState* es = NULL;

  // Continue to choose states while the there is an availible non-active state
  // that we are able to acquire a lock. Selecting a state only requires a
  // shared reader lock.
  while (((stateCount - activeStateCount) > 0) && es == NULL) {
    {
      SharedLock lock(statesMutex);
      es = states->choose(theRNG->getDoubleL()); 
    }
    if (!es->lock.try_lock()) {
      es = NULL;
    }
  }

  if (es != NULL)
    ++activeStateCount;

  return es;
}

// Same getWeight as WeightedRandomSearcher
double ParallelWeightedRandomSearcher::getWeight(ExecutionState *es) {
  switch(type) {
  default:
  case Depth: 
    return es->weight;
  case InstCount: {
    uint64_t count = theStatisticManager->getIndexedValue(stats::instructions,
                                                          es->pc->info->id);
    double inv = 1. / std::max((uint64_t) 1, count);
    return inv * inv;
  }
  case CPInstCount: {
    StackFrame &sf = es->stack.back();
    uint64_t count = sf.callPathNode->statistics.getValue(stats::instructions);
    double inv = 1. / std::max((uint64_t) 1, count);
    return inv;
  }
  case QueryCost:
    return (es->queryCost < .1) ? 1. : 1./es->queryCost;
  case CoveringNew:
  case MinDistToUncovered: {
    uint64_t md2u = computeMinDistToUncovered(es->pc,
                                              es->stack.back().minDistToUncoveredOnReturn);

    double invMD2U = 1. / (md2u ? md2u : 10000);
    if (type==CoveringNew) {
      double invCovNew = 0.;
      if (es->instsSinceCovNew)
        invCovNew = 1. / std::max(1, (int) es->instsSinceCovNew - 1000);
      return (invCovNew * invCovNew + invMD2U * invMD2U);
    } else {
      return invMD2U * invMD2U;
    }
  }
  }
}

void ParallelWeightedRandomSearcher::update(ExecutionState *current,
                                    const std::set<ExecutionState*> &addedStates,
                                    const std::set<ExecutionState*> &removedStates) {
  for (std::set<ExecutionState*>::const_iterator it = addedStates.begin(),
         ie = addedStates.end(); it != ie; ++it) {
    ExecutionState *es = *it;
    double weight = getWeight(es);
    {
      // Acquire unique writer lock
      UniqueSharedLock guard(statesMutex);
      states->insert(es, weight);
    }
    ++stateCount;
  }

  for (std::set<ExecutionState*>::const_iterator it = removedStates.begin(),
         ie = removedStates.end(); it != ie; ++it) {
    {
      // Acquire unique writer lock
      UniqueSharedLock guard(statesMutex);
      states->remove(*it);
    }
    --stateCount;
  }

  if (current) {
    if (updateWeights && !removedStates.count(current)) {
      double currentWeight;
      double weight = getWeight(current);
      {
        {
          // Acquire shared reader lock and get current weight
          SharedLock guard(statesMutex);
          currentWeight = states->getWeight(current);
        }
        if (currentWeight != weight) {
          // If weight has changed, grab writer lock
          UniqueSharedLock guard(statesMutex);
          states->update(current, weight);
        }
      }
    }
    current->lock.unlock();
    --activeStateCount;
  }
}

bool ParallelWeightedRandomSearcher::empty() { 
  return (stateCount - activeStateCount) == 0;
}

///


