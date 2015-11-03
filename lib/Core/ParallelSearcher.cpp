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
#include "ParallelSearcher.h"

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
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/raw_ostream.h"

#if LLVM_VERSION_CODE < LLVM_VERSION(3, 5)
#include "llvm/Support/CallSite.h"
#else
#include "llvm/IR/CallSite.h"
#endif

#include <cassert>
#include <fstream>
#include <climits>

///

using namespace klee;
using namespace llvm;

namespace klee {
  extern ThreadSpecificPointer<RNG>::type theRNG;
}

ExecutionState &ParallelDFSSearcher::selectState() {
  ExecutionState* state = NULL;
  stack.pop(state, true);
  return *state;
}

ExecutionState* ParallelDFSSearcher::trySelectState() {
  ExecutionState* state = NULL;
  if (stack.pop(state)) {
    return state;
  }
  return NULL;
}

void ParallelDFSSearcher::update(ExecutionState *current,
                            const std::set<ExecutionState*> &addedStates,
                            const std::set<ExecutionState*> &removedStates) {
  if (current && removedStates.count(current) == 0)
    stack.push(current, true);
  if (addedStates.size()) {
    for (auto es : addedStates) {
      stack.push(es, true);
    }
  }
}

ExecutionState* ParallelDFSSearcher::updateAndTrySelectState(ExecutionState *current,
                            const std::set<ExecutionState*> &addedStates,
                            const std::set<ExecutionState*> &removedStates) {
  update(current, addedStates, removedStates);
  return trySelectState();
}

bool ParallelDFSSearcher::empty() {
  return stack.size() == 0;
}

void ParallelDFSSearcher::printName(llvm::raw_ostream &os) {
  os << "ParallelDFSSearcher\n";
}

/// 

// Note parallel DFS and BFS differ from single threaded impl in Searcher.cpp.
// the parallel implementations pop and push current, where the single threaded
// versions maintain a static stack or queue, to better match, the st version,
// DFS should push_back current, and BFS should push_front current

ExecutionState &ParallelBFSSearcher::selectState() {
  ExecutionState* state = NULL;
  queue.pop(state, true);
  return *state;
}

ExecutionState* ParallelBFSSearcher::trySelectState() {
  ExecutionState* state = NULL;
  if (queue.pop(state)) {
    return state;
  }
  return NULL;
}

void ParallelBFSSearcher::update(ExecutionState *current,
                            const std::set<ExecutionState*> &addedStates,
                            const std::set<ExecutionState*> &removedStates) {
  if (addedStates.size()) {
    for (auto es : addedStates) {
      queue.push(es, true);
    }
  }
  if (current && removedStates.count(current) == 0)
    queue.push(current, true);
}

ExecutionState* ParallelBFSSearcher::updateAndTrySelectState(
    ExecutionState *current,
    const std::set<ExecutionState*> &addedStates,
    const std::set<ExecutionState*> &removedStates) {

  update(current, addedStates, removedStates);
  return trySelectState();
}

bool ParallelBFSSearcher::empty() {
  return queue.size() == 0;
}

void ParallelBFSSearcher::printName(llvm::raw_ostream &os) {
  os << "ParallelBFSSearcher\n";
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

void ParallelSearcher::printName(llvm::raw_ostream &os) {
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
    SharedLock lock(statesMutex);
    es = states->choose(theRNG->getDoubleL()); 

    // Race condition: Need to try to lock now, before we release shared lock.
    // T1: choose state  
    // T2: remove current state and unlock current state
    // T1: try lock succeeds,  but state is removed
    if (!es->lock.try_lock())
      es = NULL;
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

ParallelRandomPathSearcher::ParallelRandomPathSearcher(Executor &_executor)
  : executor(_executor) {
}

ParallelRandomPathSearcher::~ParallelRandomPathSearcher() {
}

ExecutionState &ParallelRandomPathSearcher::selectState() {
  unsigned flips=0, bits=0;
  PTree::Node *n = NULL;

  // Acquire lock on processTree externally
  PTree::Guard guard(*executor.processTree);
  while (n == NULL || states.count(n->data) == 0) {
    n = executor.processTree->root;
    while (!n->data) {
      if (!n->left) {
        n = n->right;
      } else if (!n->right) {
        n = n->left;
      } else {
        if (bits==0) {
          flips = theRNG->getInt32();
          bits = 32;
        }
        --bits;
        n = (flips&(1<<bits)) ? n->left : n->right;
      }
    }
  }

  return *n->data;
}

void ParallelRandomPathSearcher::update(ExecutionState *current,
                                const std::set<ExecutionState*> &addedStates,
                                const std::set<ExecutionState*> &removedStates) {
  states.insert(addedStates.begin(), addedStates.end());
  for (std::set<ExecutionState*>::const_iterator it = removedStates.begin(),
         ie = removedStates.end(); it != ie; ++it) {
    states.erase(*it);
  }
}

bool ParallelRandomPathSearcher::empty() {
  return states.empty();
}
///

static void ParallelBatchingSearcher_ExecutionState_Cleanup(ExecutionState* es) {}

__thread ExecutionState* lastState = NULL;
ParallelBatchingSearcher::ParallelBatchingSearcher(Searcher *_baseSearcher,
                                   double _timeBudget,
                                   unsigned _instructionBudget)
  : baseSearcher(_baseSearcher),
    initialTimeBudget(_timeBudget),
    initialInstructionBudget(_instructionBudget) {}
    //initialInstructionBudget(_instructionBudget),
    // reset calls delete on state otherwise
    //lastState(ParallelBatchingSearcher_ExecutionState_Cleanup) {}

ParallelBatchingSearcher::~ParallelBatchingSearcher() {
  delete baseSearcher;
}

ExecutionState* ParallelBatchingSearcher::trySelectState() {
  if (!lastStartTime.get() || !timeBudget.get() || !lastStartInstructions.get()) {

    lastStartTime.reset(new llvm::sys::TimeValue());
    *lastStartTime = llvm::sys::TimeValue::now();

    timeBudget.reset(new double());
    *timeBudget = initialTimeBudget;

    lastStartInstructions.reset(new unsigned());
    *lastStartInstructions = initialInstructionBudget;
  }

  double delta = (double)(llvm::sys::TimeValue::now() - *lastStartTime).seconds();
  //double delta = util::DurationToSeconds(util::Clock::now() - *lastStartTime).count();
  //if (!lastState.get()
  //    || (delta > (*timeBudget)) )
  //    /*|| (stats::instructions-lastStartInstructions)>instructionBudget)*/
  //{
  //  if (lastState.get()) {
  //    if (delta > ((*timeBudget) * 1.1)) {
  //      //llvm::errs() << "KLEE: increased time budget from " << *timeBudget
  //      //             << " to " << delta << "\n";
  //      *timeBudget = delta;
  //    }
  //  } else {
  //    lastState.reset(baseSearcher->trySelectState());
  //  }

  //  *lastStartTime = util::Clock::now();
  //  //*lastStartInstructions = stats::instructions;
  //}
  //return lastState.get();
  
  if (!lastState
      || (delta > (*timeBudget)) )
      /*|| (stats::instructions-lastStartInstructions)>instructionBudget)*/
  {
    if (lastState) {
      //if (delta > ((*timeBudget) * 1.1)) {
      //  llvm::errs() << "KLEE: increased time budget from " << *timeBudget
      //               << " to " << delta << "\n";
      //  *timeBudget = delta;
      //}
    } else {
      lastState = baseSearcher->trySelectState();
    }

    *lastStartTime = llvm::sys::TimeValue::now();
    //*lastStartInstructions = stats::instructions;
  }
  return lastState;
}

ExecutionState &ParallelBatchingSearcher::selectState() {
  return *trySelectState();
}

bool ParallelBatchingSearcher::empty() {
  //if (lastState.get()) {
  if (lastState) {
    return false;
  }
  return baseSearcher->empty();
}

void ParallelBatchingSearcher::update(ExecutionState *current,
                              const std::set<ExecutionState*> &addedStates,
                              const std::set<ExecutionState*> &removedStates) {
  //if (lastState.get() != current || addedStates.size() || removedStates.size()) {
  if (lastState != current || addedStates.size() || removedStates.size()) {
    //lastState.reset(0);
    lastState = 0;
    baseSearcher->update(current, addedStates, removedStates);
  }
}

ExecutionState* ParallelBatchingSearcher::updateAndTrySelectState(
    ExecutionState *current,
    const std::set<ExecutionState*> &addedStates,
    const std::set<ExecutionState*> &removedStates) {

  //if (lastState.get() != current || addedStates.size() || removedStates.size()) {
  if (lastState != current || addedStates.size() || removedStates.size()) {
    //lastState.reset(0);
    lastState = 0;
    baseSearcher->update(current, addedStates, removedStates);
    return NULL;
  }
  return trySelectState();
}

