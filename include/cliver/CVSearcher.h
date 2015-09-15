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
#include "cliver/SearcherStage.h"

#include "../Core/Searcher.h"
#include "klee/util/Mutex.h"
#include "klee/util/Atomic.h"

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

class VerifySearcher : public CVSearcher {
 public:
  VerifySearcher(ClientVerifier *cv, StateMerger* merger);
  virtual klee::ExecutionState &selectState();
  virtual void update(klee::ExecutionState *current,
                      const std::set<klee::ExecutionState*> &addedStates,
                      const std::set<klee::ExecutionState*> &removedStates);
  virtual klee::ExecutionState* updateAndTrySelectState(
      klee::ExecutionState *current,
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
  bool is_empty();
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
