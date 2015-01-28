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
