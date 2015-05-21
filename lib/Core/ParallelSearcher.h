//===-- ParallelSearcher.h ---------------------------------------*- C++ -*-===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef KLEE_PARALLEL_SEARCHER_H
#define KLEE_PARALLEL_SEARCHER_H

#include "Searcher.h"

#include "llvm/Support/raw_ostream.h"

#include "klee/Internal/System/Time.h"
#include "klee/util/Mutex.h"
#include "klee/util/Thread.h"

namespace klee {

  /// ParallelSearcher acts as wrapper around other searchers. It's only purpose is
  /// to remove every state returned by selectState from the underlying searcher
  /// and re-add it back when update is called so that multiple threads won't be
  /// provided with the same state to execute.
  class ParallelSearcher : public Searcher {
    Searcher* searcher;
    RecursiveMutex lock;

  public:
    ParallelSearcher(Searcher* searcher);
    ExecutionState &selectState();
    void update(ExecutionState *current,
                const std::set<ExecutionState*> &addedStates,
                const std::set<ExecutionState*> &removedStates);
    bool empty();
    void printName(llvm::raw_ostream &os);
    ExecutionState* trySelectState();
  };

  class ParallelDFSSearcher : public Searcher {
    LockFreeStack<ExecutionState*>::type* queue;
    std::set<ExecutionState*> removedStatesSet;
    SpinLock removedStatesLock;
    SharedMutex statesMutex;

    Atomic<int>::type queueSize;
    Atomic<int>::type removedStatesCount;

  private:
    bool checkStateRemoved(ExecutionState* state);
  public:
    ParallelDFSSearcher();
    ExecutionState &selectState();
    ExecutionState* trySelectState();
    void update(ExecutionState *current,
                const std::set<ExecutionState*> &addedStates,
                const std::set<ExecutionState*> &removedStates);
    ExecutionState* updateAndTrySelectState(ExecutionState *current,
                    const std::set<ExecutionState*> &addedStates,
                    const std::set<ExecutionState*> &removedStates);
    bool empty();
    void printName(llvm::raw_ostream &os);
  };

  class ParallelWeightedRandomSearcher : public Searcher {
  public:
    enum WeightType {
      Depth,
      QueryCost,
      InstCount,
      CPInstCount,
      MinDistToUncovered,
      CoveringNew
    };

  private:
    Executor &executor;
    DiscretePDF<ExecutionState*> *states;
    WeightType type;
    bool updateWeights;

    SharedMutex statesMutex;
    Atomic<int>::type stateCount;
    Atomic<int>::type activeStateCount;

    double getWeight(ExecutionState*);

  public:
    ParallelWeightedRandomSearcher(Executor &executor, WeightType type);
    ~ParallelWeightedRandomSearcher();

    ExecutionState &selectState();
    void update(ExecutionState *current,
                const std::set<ExecutionState*> &addedStates,
                const std::set<ExecutionState*> &removedStates);
    bool empty();
    ExecutionState* trySelectState();
    void printName(llvm::raw_ostream &os) {
      os << "ParallelWeightedRandomSearcher::";
      switch(type) {
      case Depth              : os << "Depth\n"; return;
      case QueryCost          : os << "QueryCost\n"; return;
      case InstCount          : os << "InstCount\n"; return;
      case CPInstCount        : os << "CPInstCount\n"; return;
      case MinDistToUncovered : os << "MinDistToUncovered\n"; return;
      case CoveringNew        : os << "CoveringNew\n"; return;
      default                 : os << "<unknown type>\n"; return;
      }
    }
  };

  class ParallelRandomPathSearcher : public Searcher {
    Executor &executor;
    std::set<ExecutionState*> states;

  public:
    ParallelRandomPathSearcher(Executor &_executor);
    ~ParallelRandomPathSearcher();

    ExecutionState &selectState();
    void update(ExecutionState *current,
                const std::set<ExecutionState*> &addedStates,
                const std::set<ExecutionState*> &removedStates);
    bool empty();
    void printName(llvm::raw_ostream &os) {
      os << "ParallelRandomPathSearcher\n";
    }
  };

  class ParallelBatchingSearcher : public Searcher {
    Searcher *baseSearcher;
    const unsigned initialInstructionBudget;
    const double initialTimeBudget;

    ThreadSpecificPointer<ExecutionState>::type  lastState;
    ThreadSpecificPointer<double>::type          timeBudget;
    ThreadSpecificPointer<util::TimePoint>::type lastStartTime;
    ThreadSpecificPointer<unsigned>::type        lastStartInstructions;

  public:
    ParallelBatchingSearcher(Searcher *baseSearcher, 
                     double _timeBudget,
                     unsigned _instructionBudget);
    ~ParallelBatchingSearcher();

    ExecutionState &selectState();
    ExecutionState* trySelectState();
    void update(ExecutionState *current,
                const std::set<ExecutionState*> &addedStates,
                const std::set<ExecutionState*> &removedStates);
    ExecutionState* updateAndTrySelectState(ExecutionState *current,
                                            const std::set<ExecutionState*> &addedStates,
                                            const std::set<ExecutionState*> &removedStates);
 
    bool empty();
    void printName(llvm::raw_ostream &os) {
      os << "<ParallelBatchingSearcher> timeBudget: " << initialTimeBudget
         << ", instructionBudget: " << initialInstructionBudget
         << ", baseSearcher:\n";
      baseSearcher->printName(os);
      os << "</BatchingSearcher>\n";
    }
  };

}

#endif
