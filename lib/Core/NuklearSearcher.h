/* NUKLEAR KLEE begin (ENTIRE FILE) */
//===-- NuklearSearcher.h ----------------------------------------------*- C++ -*-===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef KLEE_NUKLEAR_SEARCHER_H
#define KLEE_NUKLEAR_SEARCHER_H

#include "Searcher.h"

#include <vector>
#include <set>
#include <map>
#include <queue>

#include <ostream>

namespace llvm {
  class BasicBlock;
  class Function;
  class Instruction;
}

namespace klee {
  template<class T> class DiscretePDF;
  class ExecutionState;
  class Executor;

  class NuklearSearcher: public Searcher {
    Executor &executor;
    std::set<ExecutionState*> statesAtCheckpointMerge;
    std::set<ExecutionState*> statesAtMerge;
    std::set<ExecutionState*> mergedStates;
    Searcher *baseSearcher;
    std::vector<llvm::Function*> mergeFn;
    unsigned functionPosition;
    unsigned merge_count;
    unsigned checkpoint_count;
    unsigned max_checkpoint_count;

  private:
    llvm::Instruction *getMergePoint(ExecutionState &es, 
                                     llvm::Function *function);

  public:
    NuklearSearcher(Executor &executor, Searcher *baseSearcher);
    ~NuklearSearcher();

    ExecutionState &selectState();
    void doMerge(std::set<ExecutionState*> &states, bool pruning_on);
    void update(ExecutionState *current,
                const std::set<ExecutionState*> &addedStates,
                const std::set<ExecutionState*> &removedStates);
    bool empty() { return baseSearcher->empty() && statesAtMerge.empty(); }
    void printName(std::ostream &os) {
      os << "NuklearSearcher\n";
    }
  };

}

#endif
/* NUKLEAR KLEE end (ENTIRE FILE) */
