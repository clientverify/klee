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

class SearcherStage {
 public:
  SearcherStage() {}
  virtual ~SearcherStage() {}
  virtual CVExecutionState* next_state() = 0;
  virtual CVExecutionState* root_state() = 0;
  virtual void add_state(CVExecutionState *state) = 0;
  virtual void remove_state(CVExecutionState *state) = 0;
  virtual void notify(ExecutionEvent ev) = 0;
  virtual bool empty() = 0;
  virtual size_t cache_size() = 0;
  virtual size_t size() = 0;
  virtual void clear() = 0;
  virtual bool rebuilding() = 0;
  virtual void get_states(std::vector<ExecutionStateProperty*> &states) = 0;
  virtual void set_states(std::vector<ExecutionStateProperty*> &states) = 0;
  virtual void set_capacity(size_t c) = 0;
};

typedef std::list<SearcherStage*> SearcherStageList;

template <class Collection, class StateCache>
class SearcherStageImpl : public SearcherStage {
 
 public:
  SearcherStageImpl(CVExecutionState* root_state)
    : live_(NULL) {
    // Increment Round #
    root_state->property()->round++;
    this->add_state(root_state);
  }

  ~SearcherStageImpl() {}

  void set_capacity(size_t c) {
    cache_.set_capacity(c);
  }

  bool empty() {
    return live_ == NULL && collection_.empty() && cache_.rebuild_property() == 0;
  }

  size_t size() {
    return collection_.size();
  }

  size_t cache_size() {
    return cache_.size();
  }

  void notify(ExecutionEvent ev) {
    cache_.notify(ev);
  }

  bool rebuilding() {
    return cache_.rebuild_property() != NULL;
  }

  CVExecutionState* root_state() {
    return cache_.root_state();
  }

  CVExecutionState* next_state() {

    if (empty()) 
      return NULL;

    assert(live_ == NULL);

    live_ = cache_.rebuild_property();

    if (live_ == NULL) {
      live_ = collection_.top();
      collection_.pop();
    }

    return cache_[live_];
  }

  void add_state(CVExecutionState *state) {
    assert(state);

    if (state->property() == live_) {
      live_ = NULL;
    } else {
      assert(0 == cache_.count(state->property()));
      assert(NULL == cache_.rebuild_property());
      cache_.insert(std::make_pair(state->property(),state));
    }

    if (cache_.rebuild_property() == NULL)
      collection_.push(state->property());
  }

  // Remove the state from this stage permanently
  void remove_state(CVExecutionState *state) {
    assert(state->property() == live_);

    // Set live to null
    live_ = NULL;

    // Erase from cache
    cache_.erase(state->property());
  }

  void clear() {
    assert(!cache_.rebuild_property());
    while (!collection_.empty()) {
      CVExecutionState* state = cache_[collection_.top()];
      cache_.erase(state->property());
      collection_.pop();
      state->erase_self_permanent();
    }
    cache_.clear();
  }

  void get_states(std::vector<ExecutionStateProperty*> &states) {
    assert(!cache_.rebuild_property());
    while (!collection_.empty()) {
      states.push_back(collection_.top());
      collection_.pop();
    }
    assert(collection_.empty());
  }

  void set_states(std::vector<ExecutionStateProperty*> &states) {
    for (unsigned i=0; i<states.size(); ++i) {
      collection_.push(states[i]);
    }
  }

 protected:
  ExecutionStateProperty* live_;
  StateCache cache_;
  Collection collection_;
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

//typedef SearcherStageImpl< StatePropertyStack, BasicStateCache >          DFSSearcherStage;
//typedef SearcherStageImpl< StatePropertyQueue, BasicStateCache >          BFSSearcherStage;
//typedef SearcherStageImpl< StatePropertyPriorityQueue, BasicStateCache >  PQSearcherStage;
//typedef SearcherStageImpl< StatePropertyRandomSelector, BasicStateCache > RandomSearcherStage;

typedef SearcherStageImpl< StatePropertyStack, RebuildingStateCache >          DFSSearcherStage;
typedef SearcherStageImpl< StatePropertyQueue, RebuildingStateCache >          BFSSearcherStage;
typedef SearcherStageImpl< StatePropertyPriorityQueue, RebuildingStateCache >  PQSearcherStage;
typedef SearcherStageImpl< StatePropertyRandomSelector, RebuildingStateCache > RandomSearcherStage;

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
  virtual void check_searcher_stage_memory();

  unsigned current_stage_;
  SearcherStageList stages_;
  std::vector<CVExecutionState*> pending_states_;
  std::map<CVExecutionState*, ExecutionEvent> pending_events_;
};

////////////////////////////////////////////////////////////////////////////////

class KExtensionVerifySearcher : public VerifySearcher {
 public:
  KExtensionVerifySearcher(ClientVerifier *cv, StateMerger* merger);
  virtual klee::ExecutionState &selectState();
 private:
};

////////////////////////////////////////////////////////////////////////////////

class TrainingSearcher : public VerifySearcher {
 public:
  TrainingSearcher(ClientVerifier *cv, StateMerger* merger);
  klee::ExecutionState &selectState();
  //bool empty();
  void printName(std::ostream &os) { os << "TrainingSearcher\n"; }

  //void notify(ExecutionEvent ev); 

 private:
  //SearcherStage* get_new_stage(CVExecutionState* state);
  //void add_state(CVExecutionState* state); // has stages
  //void remove_state(CVExecutionState* state);// has stages
  //bool check_pending(CVExecutionState* state); // has pending_stages
};


////////////////////////////////////////////////////////////////////////////////

class RoundRobinTrainingSearcher : public VerifySearcher {
 public:
  RoundRobinTrainingSearcher(ClientVerifier *cv, StateMerger* merger);
  klee::ExecutionState &selectState();
  bool empty();
  void printName(std::ostream &os) { os << "RoundRobinTrainingSearcher\n"; }

  void notify(ExecutionEvent ev);

 private:
  SearcherStage* get_new_stage(CVExecutionState* state);
  void add_state(CVExecutionState* state);
  void remove_state(CVExecutionState* state);
  bool check_pending(CVExecutionState* state);

  std::map<unsigned, SearcherStageList> stage_map_;
  unsigned current_stage_round_;
};

////////////////////////////////////////////////////////////////////////////////

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
