//===-- CVSearcher.h --------------------------------------------*- C++ -*-===//
//
// <insert license>
//
//===----------------------------------------------------------------------===//
//
// TODO: make CVSearcher Pure virtual
//
//===----------------------------------------------------------------------===//
#ifndef CV_SEARCHER_H
#define CV_SEARCHER_H

#include "ClientVerifier.h" // For CliverEvent::Type
#include "ExecutionStateProperty.h"
#include "ExecutionObserver.h"

#include "../Core/Searcher.h"

#include <stack>
#include <queue>

namespace cliver {
class CVExecutionState;
class StateMerger;
class PathManagerSet;

////////////////////////////////////////////////////////////////////////////////

class CVSearcher : public klee::Searcher, public ExecutionObserver {
 public:
	CVSearcher(klee::Searcher* base_searcher, StateMerger* merger);

	virtual klee::ExecutionState &selectState();

	virtual void update(klee::ExecutionState *current,
							const std::set<klee::ExecutionState*> &addedStates,
							const std::set<klee::ExecutionState*> &removedStates);

	virtual bool empty();

	virtual void printName(std::ostream &os) { os << "CVSearcher\n"; }

  virtual void notify(ExecutionEvent ev) {}

 protected:
	klee::Searcher* base_searcher_;
	StateMerger* merger_;
};

////////////////////////////////////////////////////////////////////////////////

class LogIndexSearcher : public CVSearcher { 
 public:
	LogIndexSearcher(klee::Searcher* base_searcher, StateMerger* merger);

	klee::ExecutionState &selectState();

	void update(klee::ExecutionState *current,
							const std::set<klee::ExecutionState*> &addedStates,
							const std::set<klee::ExecutionState*> &removedStates);

	bool empty();

	void printName(std::ostream &os) { os << "LogIndexSearcher\n"; }

  void notify(ExecutionEvent ev);

 protected:
	int state_count();
	ExecutionStatePropertyMap states_;
	klee::Searcher* base_searcher_;
	StateMerger* merger_;
};

////////////////////////////////////////////////////////////////////////////////

class TrainingSearcher : public CVSearcher {
 public:
	TrainingSearcher(klee::Searcher* base_searcher, StateMerger* merger);

	klee::ExecutionState &selectState();

	void update(klee::ExecutionState *current,
							const std::set<klee::ExecutionState*> &addedStates,
							const std::set<klee::ExecutionState*> &removedStates);

	bool empty();

	void record_path(CVExecutionState *state);

	void printName(std::ostream &os) { os << "TrainingSearcher\n"; }

  void notify(ExecutionEvent ev);

 private:
	ExecutionStateSet phases_[PathProperty::EndPhase];
	PathManagerSet *paths_;
};

////////////////////////////////////////////////////////////////////////////////

/// Each SearcherStage object holds ExecutionStates, where all the states in a
/// given SearcherStage have processed network events 1 to i, where i is equal
/// among all the states. Each SearcherStage has a single root state from which
/// all of the other states began execution.

enum SearcherStageMode {
  RandomSearcherStageMode,
  PQSearcherStageMode,
  DFSSearcherStageMode,
  BFSSearcherStageMode,
};

class SearcherStage {
 public:
  SearcherStage() {}
  virtual ~SearcherStage() {}
  virtual CVExecutionState* next_state() = 0;
  virtual void add_state(CVExecutionState *state) = 0;
  virtual void remove_state(CVExecutionState *state) = 0;
  virtual bool empty() = 0;
};

typedef std::list<SearcherStage*> SearcherStageList;

template <class Collection>
class BasicSearcherStage : public SearcherStage {
 public:
  BasicSearcherStage(CVExecutionState* root_state)
    : live_state_(NULL) {
    this->add_state(root_state);
  }

  virtual ~BasicSearcherStage() {}

  virtual bool empty() {
    return state_set_.empty();
  }

  virtual CVExecutionState* next_state() {
    if (empty()) return NULL;
    assert(live_state_ == NULL);
    live_state_ = states_.top();
    states_.pop();
    return live_state_;
  }

  virtual void add_state(CVExecutionState *state) {
    assert(state);
    if (state == live_state_) {
      live_state_ = NULL;
    } else {
      assert(!state_set_.count(state));
      state_set_.insert(state);
    }
    states_.push(state);
  }

  virtual void remove_state(CVExecutionState *state) {
    assert(state == live_state_);
    live_state_ = NULL;
    state_set_.erase(state);
  }

 protected:
  CVExecutionState* live_state_;
  ExecutionStateSet state_set_;
  Collection states_;
};

class ExecutionStateQueue : public std::queue<CVExecutionState*> {
 public:
  CVExecutionState* top() { return front(); }
};

class ExecutionStateRandomSelector : public std::vector<CVExecutionState*> {
 public:
  ExecutionStateRandomSelector() : size_(0) {}

  CVExecutionState* top() { return random_swap(); }

  void pop() { size_ = std::max(0, size_-1); }

  void push(CVExecutionState* state) {
    if (size_ == size())
      push_back(state);
    else
      at(size_) = state;
    size_++;
  }

 private:
  CVExecutionState* random_swap() {
    std::swap(at(rand() % size_), at(size_-1));
    return at(size_-1);
  }

  int size_;
};

typedef BasicSearcherStage<std::stack<CVExecutionState*> > DFSSearcherStage;
typedef BasicSearcherStage<ExecutionStateQueue>            BFSSearcherStage;
typedef BasicSearcherStage<ExecutionStatePriorityQueue>    PQSearcherStage;
typedef BasicSearcherStage<ExecutionStateRandomSelector>   RandomSearcherStage;

////////////////////////////////////////////////////////////////////////////////

class VerifySearcher : public CVSearcher {
 public:
  VerifySearcher(StateMerger* merger);
  klee::ExecutionState &selectState();
  void update(klee::ExecutionState *current,
              const std::set<klee::ExecutionState*> &addedStates,
              const std::set<klee::ExecutionState*> &removedStates);
  bool empty();
  void printName(std::ostream &os) { os << "VerifySearcher\n"; }

  void notify(ExecutionEvent ev);

 private:
  SearcherStage* get_new_stage(CVExecutionState* state);
  void add_state(CVExecutionState* state);
  void remove_state(CVExecutionState* state);
  bool check_pending(CVExecutionState* state);

  StateMerger* merger_;
  SearcherStageList stages_;
  SearcherStageList pending_stages_;
  ExecutionStateSet pending_states_;
};

class SearcherStageFactory {
 public:
  static SearcherStage* create(StateMerger* merger, CVExecutionState* state);
};

} // end namespace cliver
#endif // CV_SEARCHER_H
