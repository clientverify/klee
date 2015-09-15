//===-- SearcherStage.h -----------------------------------------*- C++ -*-===//
//
// <insert license>
//
//===----------------------------------------------------------------------===//
//
//===----------------------------------------------------------------------===//
#ifndef CV_SEARCHER_STAGE_H
#define CV_SEARCHER_STAGE_H

#include "cliver/ExecutionStateProperty.h"
#include "cliver/StateRebuilder.h"
#include <boost/unordered_map.hpp>
#include <stack>
#include <queue>
#include <vector>

namespace cliver {

/// Each SearcherStage object holds ExecutionStates, where all the states in a
/// given SearcherStage have processed network events 1 to i, where i is equal
/// among all the states. Each SearcherStage has a single root state from which
/// all of the other states began execution.

class SearcherStage {
 public:
  SearcherStage() : parent(0), multi_pass_parent(0) {}
  virtual ~SearcherStage() {}
  virtual CVExecutionState* next_state() = 0;
  virtual CVExecutionState* root_state() = 0;
  virtual void add_state(CVExecutionState *state) = 0;
  virtual void remove_state(CVExecutionState *state) = 0;
  virtual void notify(ExecutionEvent ev) = 0;
  virtual bool empty() = 0;
  virtual size_t cache_size() = 0;
  virtual size_t size() = 0;
  virtual size_t live_count() = 0;
  virtual void clear() = 0;
  virtual bool rebuilding() = 0;
  virtual void get_states(std::vector<ExecutionStateProperty*> &states) = 0;
  virtual void set_states(std::vector<ExecutionStateProperty*> &states) = 0;
  virtual void set_capacity(size_t c) = 0;
  virtual void print(std::ostream &os)  = 0;

  SearcherStage *parent;
  SearcherStage *multi_pass_parent;
  std::vector<CVExecutionState*> leaf_states;
};

typedef std::list<SearcherStage*> SearcherStageList;

////////////////////////////////////////////////////////////////////////////////

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

  size_t live_count() {
    return live_ ? 1 : 0;
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
  SearcherStageThreadedImpl(CVExecutionState* root) : size_(0) {
    if (root != NULL) {
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
    //assert(size_ == collection_.size());
    return size_;
  }

  size_t live_count() {
    return live_set_.size();
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

    if (collection_.empty()) {
      return NULL;
    }
    --size_;
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

    if (cache_.rebuild_property() == NULL) {
      collection_.push(state->property());
      ++size_;
    }
  }

  // Remove the state from this stage permanently
  void remove_state(CVExecutionState *state) {
    //assert(state && "Null state removal");
    //assert(state->property() && "Null state property removal");
    //assert(live_set_.count(state->property()) > 0 && "state removal, not found in live_set");
    //if ((live_set_.count(state->property()) > 0)) {
    //  printf("state removal, not found in live_set\n");
    //}

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
      --size_;
      collection_.pop();
      state->erase_self_permanent();
    }
    cache_.clear();
  }

  void get_states(std::vector<ExecutionStateProperty*> &states) {
    //assert(!cache_.rebuild_property());
    while (!collection_.empty()) {
      //assert(collection_.top()->edit_distance == INT_MAX);
      states.push_back(collection_.top());
      --size_;
      collection_.pop();
    }
    assert(collection_.empty());
  }

  void print(std::ostream &os) {
    os << "-SearcherStage----\n";
    for (auto p : live_set_) {
      CVExecutionState* state = cache_[p];
      //os << "Live: " << p << " " << *p << "\n";
      os << "Live: " << state << " " << *state << "\n";
    }
    std::vector<ExecutionStateProperty*> states;
    get_states(states);
    for (auto p : states) {
      CVExecutionState* state = cache_[p];
      //os << "State: " << p << " " << *p << "\n";
      os << "State: " << state << " " << *state << "\n";
    }
    set_states(states);
    os << "------------------\n";
  }

  void set_states(std::vector<ExecutionStateProperty*> &states) {
    for (unsigned i=0; i<states.size(); ++i) {
      collection_.push(states[i]);
      ++size_;
    }
  }

 protected:
  std::set<ExecutionStateProperty*> live_set_;
  StateCache cache_;
  Collection collection_;
  klee::Atomic<size_t>::type size_;
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

}

#endif
