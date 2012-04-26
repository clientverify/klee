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

#include "cliver/ClientVerifier.h" // For CliverEvent::Type
#include "cliver/CVExecutionState.h"
#include "cliver/CVExecutor.h"
#include "cliver/ExecutionStateProperty.h"
#include "cliver/ExecutionObserver.h"
#include "cliver/StateRebuilder.h"

#include "klee/Searcher.h"

#include <boost/unordered_map.hpp>
#include <stack>
#include <queue>

#include "llvm/Support/raw_ostream.h"

namespace cliver {
class CVExecutionState;
class StateMerger;

////////////////////////////////////////////////////////////////////////////////

class CVSearcher : public klee::Searcher, public ExecutionObserver {
 public:
	CVSearcher(klee::Searcher* base_searcher, ClientVerifier* cv, 
             StateMerger* merger);

	virtual klee::ExecutionState &selectState();

	virtual void update(klee::ExecutionState *current,
							const std::set<klee::ExecutionState*> &addedStates,
							const std::set<klee::ExecutionState*> &removedStates);

	virtual bool empty();

	virtual void printName(std::ostream &os) { os << "CVSearcher\n"; }

  virtual void notify(ExecutionEvent ev) {}

 protected:
	klee::Searcher* base_searcher_;
  ClientVerifier* cv_;
	StateMerger* merger_;
};

////////////////////////////////////////////////////////////////////////////////

/// Each SearcherStage object holds ExecutionStates, where all the states in a
/// given SearcherStage have processed network events 1 to i, where i is equal
/// among all the states. Each SearcherStage has a single root state from which
/// all of the other states began execution.

struct CVExecutionStateDeleter;

class SearcherStage {
 public:
  SearcherStage() {}
  virtual ~SearcherStage() {}
  virtual CVExecutionState* next_state() = 0;
  virtual void add_state(CVExecutionState *state) = 0;
  virtual void remove_state(CVExecutionState *state) = 0;
  virtual bool empty() = 0;
  virtual void notify(ExecutionEvent ev) = 0;
  virtual void cache_erase(CVExecutionState *state) = 0; //REMOVE
  virtual void clear(CVExecutionStateDeleter* cv_deleter) = 0;
};

typedef std::list<SearcherStage*> SearcherStageList;

template <class Collection>
class SearcherStageImpl : public SearcherStage {
 typedef boost::unordered_map<ExecutionStateProperty*, CVExecutionState*> Cache;
 public:
  SearcherStageImpl(CVExecutionState* root_state)
    : live_(NULL) {
    this->add_state(root_state);
    rebuilder_.set_root(root_state);
  }

  ~SearcherStageImpl() {}

  bool empty() {
    return !rebuilder_.active() && live_ == NULL && collection_.empty();
  }

  inline void notify(ExecutionEvent ev) {
    rebuilder_.notify(ev);
  }

  CVExecutionState* next_state() {
    if (rebuilder_.active())
      return rebuilder_.state();

    if (empty()) 
      return NULL;

    assert(live_ == NULL);

    live_ = collection_.top();
    collection_.pop();

    if (cache_.count(live_) == 0)
      cache_[live_] = rebuilder_.rebuild(live_);

    return cache_[live_];
  }

  void add_state(CVExecutionState *state) {
    assert(state);

    if (rebuilder_.active()) {
      assert(rebuilder_.state() == state);
      return;
    }

    if (state->property() == live_) {
      live_ = NULL;
    } else {
      assert(0 == cache_.count(state->property()));
      cache_[state->property()] = state;
    }
    collection_.push(state->property());
  }

  void remove_state(CVExecutionState *state) {
    assert(!rebuilder_.active());
    assert(state->property() == live_);
    live_ = NULL;
    cache_.erase(state->property());
  }

  void cache_erase(CVExecutionState *state) {
    cache_.erase(state->property());
    state->set_property(NULL);
    state->cv()->executor()->remove_state_internal_without_notify(state);
  }

  void clear_lru() {
    Cache::iterator it=cache_.begin(), ie = cache_.end();
    for (; it != ie; ++it)
      cache_erase(it->second);

    cache_.clear();
  }

  void clear(CVExecutionStateDeleter* cv_deleter=NULL) {
    assert(!rebuilder_.active());
    while (!collection_.empty()) {
      CVExecutionState* state = cache_[collection_.top()];
      cache_.erase(state->property());
      state->cv()->executor()->remove_state_internal(state);
      collection_.pop();
    }
    cache_.clear();
  }

 protected:
  ExecutionStateProperty* live_;
  Cache cache_;
  Collection collection_;
  StateRebuilder rebuilder_;
};

class StatePropertyStack 
  : public std::stack<ExecutionStateProperty*> {
};

class StatePropertyQueue 
  : public std::queue<ExecutionStateProperty*> {
 public:
  ExecutionStateProperty* top() { return front(); }
};

class StatePropertyRandomSelector 
  : public std::vector<ExecutionStateProperty*> {
 public:
  StatePropertyRandomSelector () : size_(0) {}

  ExecutionStateProperty* top() { return random_swap(); }

  void pop() { size_ = std::max(0, size_-1); }

  void push(ExecutionStateProperty* property) {
    if (size_ == (int)size())
      push_back(property);
    else
      at(size_) = property;
    size_++;
  }

 private:
  ExecutionStateProperty* random_swap() {
    std::swap(at(rand() % size_), at(size_-1));
    return at(size_-1);
  }
  int size_;
};

typedef SearcherStageImpl< StatePropertyStack >          DFSSearcherStage;
typedef SearcherStageImpl< StatePropertyQueue >          BFSSearcherStage;
typedef SearcherStageImpl< StatePropertyPriorityQueue >  PQSearcherStage;
typedef SearcherStageImpl< StatePropertyRandomSelector > RandomSearcherStage;

////////////////////////////////////////////////////////////////////////////////

class VerifySearcher : public CVSearcher {
 public:
  VerifySearcher(ClientVerifier *cv, StateMerger* merger);
  virtual klee::ExecutionState &selectState();
  virtual void update(klee::ExecutionState *current,
                      const std::set<klee::ExecutionState*> &addedStates,
                      const std::set<klee::ExecutionState*> &removedStates);
  virtual bool empty();
  virtual void printName(std::ostream &os) { os << "VerifySearcher\n"; }

  virtual void notify(ExecutionEvent ev);

 protected:
  virtual SearcherStage* get_new_stage(CVExecutionState* state);
  virtual void add_state(CVExecutionState* state);
  virtual void remove_state(CVExecutionState* state);
  virtual bool check_pending(CVExecutionState* state);

  SearcherStageList stages_;
  SearcherStageList pending_stages_;
  ExecutionStateSet pending_states_;
};

////////////////////////////////////////////////////////////////////////////////

class MergeVerifySearcher : public VerifySearcher {
 public:
  MergeVerifySearcher(ClientVerifier *cv, StateMerger* merger);
  void notify(ExecutionEvent ev);
  bool check_pending(CVExecutionState* state);
 private:
};

////////////////////////////////////////////////////////////////////////////////

class TrainingSearcher : public CVSearcher {
 public:
  TrainingSearcher(ClientVerifier *cv, StateMerger* merger);
  klee::ExecutionState &selectState();
  void update(klee::ExecutionState *current,
              const std::set<klee::ExecutionState*> &addedStates,
              const std::set<klee::ExecutionState*> &removedStates);
  bool empty();
  void printName(std::ostream &os) { os << "TrainingSearcher\n"; }

  void notify(ExecutionEvent ev);

 private:
  SearcherStage* get_new_stage(CVExecutionState* state);
  void add_state(CVExecutionState* state);
  void remove_state(CVExecutionState* state);
  bool check_pending(CVExecutionState* state);

  SearcherStageList stages_;
  SearcherStageList pending_stages_;
  ExecutionStateSet pending_states_;
  ExecutionStateSet pruned_states_;
};

class SearcherStageFactory {
 public:
  static SearcherStage* create(StateMerger* merger, CVExecutionState* state);
};

class CVSearcherFactory {
 public:
  static CVSearcher* create(klee::Searcher* base_searcher, 
                            ClientVerifier* cv, StateMerger* merger);
};

} // end namespace cliver
#endif // CV_SEARCHER_H
