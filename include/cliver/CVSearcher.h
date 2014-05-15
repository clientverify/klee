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

#include "../Core/Searcher.h"
#include "klee/util/Mutex.h"

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
  klee::Mutex lock_;
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
  SearcherStageImpl(CVExecutionState* root)
    : live_(NULL) {

    // Set new root state
    cache_.set_root(root);

    // Clone root state
    ExecutionStateProperty* property_clone = root->property()->clone();

    assert(property_clone != root->property());
    CVExecutionState* root_clone = root->clone(property_clone);

    // Hack: add cloned state to the internal states set in Executor
    root->cv()->executor()->add_state_internal(root_clone);

    // Insert clone of root state
    this->add_state(root_clone);
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
      assert(collection_.top()->edit_distance == INT_MAX);
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

template <class Collection, class StateCache>
class SearcherStageThreadedImpl : public SearcherStage {

 public:
  SearcherStageThreadedImpl(CVExecutionState* root) {

    // Set new root state
    cache_.set_root(root);

    // Clone root state
    ExecutionStateProperty* property_clone = root->property()->clone();

    assert(property_clone != root->property());
    CVExecutionState* root_clone = root->clone(property_clone);

    // Hack: add cloned state to the internal states set in Executor
    //root->cv()->executor()->add_state_internal(root_clone);

    // Insert clone of root state
    this->add_state(root_clone);
  }

  ~SearcherStageThreadedImpl() {}

  void set_capacity(size_t c) {
    cache_.set_capacity(c);
  }

  bool empty() {
    return live_set_.empty() &&
           collection_.empty() &&
           cache_.rebuild_property() == 0;
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

    //assert(live_ == NULL);

    // XXX FIXME support rebuilding
    //live_ = cache_.rebuild_property();
    //if (live_ == NULL) {
    //  live_ = collection_.top();
    //  collection_.pop();
    //}
    //return cache_[live_];

    if (collection_.empty())
    {
      return NULL;
    }
    ExecutionStateProperty* next = collection_.top();
    collection_.pop();
    live_set_.insert(next);

    return cache_[next];
  }

  void add_state(CVExecutionState *state) {
    assert(state);

    if (live_set_.count(state->property()) > 0) {
      live_set_.erase(state->property());
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
    assert(live_set_.count(state->property()) > 0);

    // Set live to null
    live_set_.erase(state->property());

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
    //assert(!cache_.rebuild_property());
    while (!collection_.empty()) {
      assert(collection_.top()->edit_distance == INT_MAX);
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
  std::set<ExecutionStateProperty*> live_set_;
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
  ExecutionStateProperty* top() { 
    std::swap(at(rand() % size()), back());
    return back();
  }

  void pop() { pop_back(); }
  void push(ExecutionStateProperty* property) { push_back(property); }
};

// TODO FIXME support rebuilding
//typedef SearcherStageImpl<
//  StatePropertyStack, RebuildingStateCache >          DFSSearcherStage;
//typedef SearcherStageImpl<
//  StatePropertyQueue, RebuildingStateCache >          BFSSearcherStage;
//typedef SearcherStageImpl<
//  StatePropertyPriorityQueue, RebuildingStateCache >  PQSearcherStage;
//typedef SearcherStageImpl<
//  StatePropertyRandomSelector, RebuildingStateCache > RandomSearcherStage;

typedef SearcherStageThreadedImpl<
  StatePropertyStack, BasicStateCache >          DFSSearcherStage;
typedef SearcherStageThreadedImpl<
  StatePropertyQueue, BasicStateCache >          BFSSearcherStage;
typedef SearcherStageThreadedImpl<
  StatePropertyPriorityQueue, BasicStateCache >  PQSearcherStage;
typedef SearcherStageThreadedImpl<
  StatePropertyRandomSelector, BasicStateCache > RandomSearcherStage;

////////////////////////////////////////////////////////////////////////////////

class VerifySearcher : public CVSearcher {
 public:
  VerifySearcher(ClientVerifier *cv, StateMerger* merger);
  virtual klee::ExecutionState &selectState();
  virtual void update(klee::ExecutionState *current,
                      const std::set<klee::ExecutionState*> &addedStates,
                      const std::set<klee::ExecutionState*> &removedStates);
  virtual bool empty();
  virtual klee::ExecutionState* trySelectState();
  virtual void printName(std::ostream &os) { os << "VerifySearcher\n"; }

  virtual void notify(ExecutionEvent ev);

 protected:
  virtual SearcherStage* get_new_stage(CVExecutionState* state);
  virtual void add_state(CVExecutionState* state);
  virtual void remove_state(CVExecutionState* state);
  virtual bool check_pending(CVExecutionState* state);
  virtual void process_unique_pending_states();
  virtual void clear_caches();
  virtual SearcherStage* select_stage();
  virtual CVExecutionState* check_state_property(SearcherStage*, CVExecutionState*);

  void remove_pending_duplicates();
  SearcherStage* create_and_add_stage(CVExecutionState* state);

  SearcherStage* current_stage_;
  SearcherStage* last_stage_cleared_;
  unsigned current_round_;
  unsigned max_active_round_;
  bool at_kprefix_max_;
  ExecutionStateProperty *prev_property_;
  bool prev_property_removed_;
  SearcherStageList stages_;
  std::vector<CVExecutionState*> pending_states_;
  std::map<CVExecutionState*, ExecutionEvent> pending_events_;
  std::vector<SearcherStageList*> new_stages_;
};

////////////////////////////////////////////////////////////////////////////////

class TrainingSearcher : public VerifySearcher {
 public:
  TrainingSearcher(ClientVerifier *cv, StateMerger* merger);
  klee::ExecutionState &selectState();
  klee::ExecutionState* trySelectState();
  void printName(std::ostream &os) { os << "TrainingSearcher\n"; }

 protected:
  virtual SearcherStage* select_stage();
};

////////////////////////////////////////////////////////////////////////////////

class SearcherStageFactory {
 public:
  static SearcherStage* create(StateMerger* merger, CVExecutionState* state);
};

////////////////////////////////////////////////////////////////////////////////

class CVSearcherFactory {
 public:
  static CVSearcher* create(klee::Searcher* base_searcher, 
                            ClientVerifier* cv, StateMerger* merger);
};

////////////////////////////////////////////////////////////////////////////////

} // end namespace cliver
#endif // CV_SEARCHER_H
