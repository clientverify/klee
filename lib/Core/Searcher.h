//===-- Searcher.h ----------------------------------------------*- C++ -*-===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef KLEE_SEARCHER_H
#define KLEE_SEARCHER_H

#include "klee/util/Mutex.h"

#include <vector>
#include <set>
#include <map>
#include <queue>
#include <stack>

// FIXME: Move out of header, use llvm streams.
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

  class Searcher {
  public:
    virtual ~Searcher();

    virtual ExecutionState &selectState() = 0;

    virtual void update(ExecutionState *current,
                        const std::set<ExecutionState*> &addedStates,
                        const std::set<ExecutionState*> &removedStates) = 0;

    virtual bool empty() = 0;

    // prints name of searcher as a klee_message()
    // TODO: could probably make prettier or more flexible
    virtual void printName(std::ostream &os) { 
      os << "<unnamed searcher>\n";
    }

    // pgbovine - to be called when a searcher gets activated and
    // deactivated, say, by a higher-level searcher; most searchers
    // don't need this functionality, so don't have to override.
    virtual void activate() {}
    virtual void deactivate() {}

    // utility functions

    void addState(ExecutionState *es, ExecutionState *current = 0) {
      std::set<ExecutionState*> tmp;
      tmp.insert(es);
      update(current, tmp, std::set<ExecutionState*>());
    }

    void removeState(ExecutionState *es, ExecutionState *current = 0) {
      std::set<ExecutionState*> tmp;
      tmp.insert(es);
      update(current, std::set<ExecutionState*>(), tmp);
    }

    // trySelectState and updateAndTrySelectState are wrappers around 
    // selectState(), update() and empty(). Thread-aware Searcher 
    // implmentations override them if needed.

    virtual ExecutionState* trySelectState() {
      if (!empty()) {
        return &(selectState());
      }
      return NULL;
    }

    // updateAndTrySelectState() will always call update(), but may or may not call
    // trySelectState(), the default is to return NULL.
    // Note: update, followed by trySelectState will fail with BatchingSearcher
    virtual ExecutionState* updateAndTrySelectState(ExecutionState *current,
                                                    const std::set<ExecutionState*> &addedStates,
                                                    const std::set<ExecutionState*> &removedStates) {
      update(current, addedStates, removedStates);
      return NULL;
    }

    // The searcher holds the authoritative collection of all states
    virtual std::set<ExecutionState*> states() {
      std::stack<ExecutionState*> stack;
      std::set<ExecutionState*> states;

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

    enum CoreSearchType {
      DFS,
      ParallelDFS,
      BFS,
      RandomState,
      RandomPath,
      NURS_CovNew,
      NURS_MD2U,
      NURS_Depth,
      NURS_ICnt,
      NURS_CPICnt,
      NURS_QC
    };
  };

  class DFSSearcher : public Searcher {
    std::vector<ExecutionState*> states;

  public:
    ExecutionState &selectState();
    void update(ExecutionState *current,
                const std::set<ExecutionState*> &addedStates,
                const std::set<ExecutionState*> &removedStates);
    bool empty() { return states.empty(); }
    void printName(std::ostream &os) {
      os << "DFSSearcher\n";
    }
  };

  class BFSSearcher : public Searcher {
    std::deque<ExecutionState*> states;

  public:
    ExecutionState &selectState();
    void update(ExecutionState *current,
                const std::set<ExecutionState*> &addedStates,
                const std::set<ExecutionState*> &removedStates);
    bool empty() { return states.empty(); }
    void printName(std::ostream &os) {
      os << "BFSSearcher\n";
    }
  };

  class RandomSearcher : public Searcher {
    std::vector<ExecutionState*> states;

  public:
    ExecutionState &selectState();
    void update(ExecutionState *current,
                const std::set<ExecutionState*> &addedStates,
                const std::set<ExecutionState*> &removedStates);
    bool empty() { return states.empty(); }
    void printName(std::ostream &os) {
      os << "RandomSearcher\n";
    }
  };

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
    void printName(std::ostream &os);
    ExecutionState* trySelectState();
    std::set<ExecutionState*> states();
  };

  class ParallelDFSSearcher : public Searcher {
    LockFreeStack<ExecutionState*>::type* queue;
    std::set<ExecutionState*> removedStatesSet;
    SpinLock removedStatesLock;

    Atomic<int>::type queueSize;
    Atomic<int>::type removedStatesCount;

  private:
    bool checkStateRemoved(ExecutionState* state);
  public:
    ParallelDFSSearcher();
    ExecutionState &selectState();
    void update(ExecutionState *current,
                const std::set<ExecutionState*> &addedStates,
                const std::set<ExecutionState*> &removedStates);
    ExecutionState* updateAndTrySelectState(ExecutionState *current,
                    const std::set<ExecutionState*> &addedStates,
                    const std::set<ExecutionState*> &removedStates);
    bool empty();
    void printName(std::ostream &os);
  };


  class WeightedRandomSearcher : public Searcher {
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
    
    double getWeight(ExecutionState*);

  public:
    WeightedRandomSearcher(Executor &executor, WeightType type);
    ~WeightedRandomSearcher();

    ExecutionState &selectState();
    void update(ExecutionState *current,
                const std::set<ExecutionState*> &addedStates,
                const std::set<ExecutionState*> &removedStates);
    bool empty();
    void printName(std::ostream &os) {
      os << "WeightedRandomSearcher::";
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

  class RandomPathSearcher : public Searcher {
    Executor &executor;
    std::set<ExecutionState*> states;

  public:
    RandomPathSearcher(Executor &_executor);
    ~RandomPathSearcher();

    ExecutionState &selectState();
    void update(ExecutionState *current,
                const std::set<ExecutionState*> &addedStates,
                const std::set<ExecutionState*> &removedStates);
    bool empty();
    void printName(std::ostream &os) {
      os << "RandomPathSearcher\n";
    }
  };

  class MergingSearcher : public Searcher {
    Executor &executor;
    std::set<ExecutionState*> statesAtMerge;
    Searcher *baseSearcher;
    llvm::Function *mergeFunction;

  private:
    llvm::Instruction *getMergePoint(ExecutionState &es);

  public:
    MergingSearcher(Executor &executor, Searcher *baseSearcher);
    ~MergingSearcher();

    ExecutionState &selectState();
    void update(ExecutionState *current,
                const std::set<ExecutionState*> &addedStates,
                const std::set<ExecutionState*> &removedStates);
    bool empty() { return baseSearcher->empty() && statesAtMerge.empty(); }
    void printName(std::ostream &os) {
      os << "MergingSearcher\n";
    }
  };

  class BumpMergingSearcher : public Searcher {
    Executor &executor;
    std::map<llvm::Instruction*, ExecutionState*> statesAtMerge;
    Searcher *baseSearcher;
    llvm::Function *mergeFunction;

  private:
    llvm::Instruction *getMergePoint(ExecutionState &es);

  public:
    BumpMergingSearcher(Executor &executor, Searcher *baseSearcher);
    ~BumpMergingSearcher();

    ExecutionState &selectState();
    void update(ExecutionState *current,
                const std::set<ExecutionState*> &addedStates,
                const std::set<ExecutionState*> &removedStates);
    bool empty() { return baseSearcher->empty() && statesAtMerge.empty(); }
    void printName(std::ostream &os) {
      os << "BumpMergingSearcher\n";
    }
  };

  class BatchingSearcher : public Searcher {
    Searcher *baseSearcher;
    double timeBudget;
    unsigned instructionBudget;

    ExecutionState *lastState;
    double lastStartTime;
    unsigned lastStartInstructions;

  public:
    BatchingSearcher(Searcher *baseSearcher, 
                     double _timeBudget,
                     unsigned _instructionBudget);
    ~BatchingSearcher();

    ExecutionState &selectState();
    void update(ExecutionState *current,
                const std::set<ExecutionState*> &addedStates,
                const std::set<ExecutionState*> &removedStates);
    bool empty() { return baseSearcher->empty(); }
    void printName(std::ostream &os) {
      os << "<BatchingSearcher> timeBudget: " << timeBudget
         << ", instructionBudget: " << instructionBudget
         << ", baseSearcher:\n";
      baseSearcher->printName(os);
      os << "</BatchingSearcher>\n";
    }
  };

  class IterativeDeepeningTimeSearcher : public Searcher {
    Searcher *baseSearcher;
    double time, startTime;
    std::set<ExecutionState*> pausedStates;

  public:
    IterativeDeepeningTimeSearcher(Searcher *baseSearcher);
    ~IterativeDeepeningTimeSearcher();

    ExecutionState &selectState();
    void update(ExecutionState *current,
                const std::set<ExecutionState*> &addedStates,
                const std::set<ExecutionState*> &removedStates);
    bool empty() { return baseSearcher->empty() && pausedStates.empty(); }
    void printName(std::ostream &os) {
      os << "IterativeDeepeningTimeSearcher\n";
    }
  };

  class InterleavedSearcher : public Searcher {
    typedef std::vector<Searcher*> searchers_ty;

    searchers_ty searchers;
    unsigned index;

  public:
    explicit InterleavedSearcher(const searchers_ty &_searchers);
    ~InterleavedSearcher();

    ExecutionState &selectState();
    void update(ExecutionState *current,
                const std::set<ExecutionState*> &addedStates,
                const std::set<ExecutionState*> &removedStates);
    bool empty() { return searchers[0]->empty(); }
    void printName(std::ostream &os) {
      os << "<InterleavedSearcher> containing "
         << searchers.size() << " searchers:\n";
      for (searchers_ty::iterator it = searchers.begin(), ie = searchers.end();
           it != ie; ++it)
        (*it)->printName(os);
      os << "</InterleavedSearcher>\n";
    }
  };

}

#endif
