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

namespace cliver {
class CVExecutionState;
class StateMerger;

////////////////////////////////////////////////////////////////////////////////
// CVSearcher:
// Abstract class which extends klee::Searcher with notify() and flush()
////////////////////////////////////////////////////////////////////////////////

class CVSearcher : public klee::Searcher, public ExecutionObserver {
 public:
  virtual ~CVSearcher() {}

  virtual klee::ExecutionState &selectState() = 0;

  virtual void update(klee::ExecutionState *current,
              const std::set<klee::ExecutionState*> &addedStates,
              const std::set<klee::ExecutionState*> &removedStates) = 0;

  virtual bool empty() = 0;

  virtual void printName(llvm::raw_ostream &os) { os << "CVSearcher\n"; }

  virtual klee::ExecutionState* updateAndTrySelectState(
      klee::ExecutionState *current,
      const std::set<klee::ExecutionState*> &addedStates,
      const std::set<klee::ExecutionState*> &removedStates) = 0;

  virtual klee::ExecutionState* trySelectState() = 0;

  virtual void notify(ExecutionEvent ev) = 0;

  virtual void flush() = 0;

  virtual void set_parent_searcher(CVSearcher* parent) = 0;
};

////////////////////////////////////////////////////////////////////////////////
// VerifySearcher: implements CVSearcher interface and is the base cliver
// searcher
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

  virtual klee::ExecutionState* trySelectState();

  virtual bool empty();

  virtual void printName(llvm::raw_ostream &os) { os << "VerifySearcher\n"; }

  virtual void notify(ExecutionEvent ev);

  virtual void flush () { }

  virtual void set_parent_searcher(CVSearcher* parent) { parent_searcher_ = parent; }

  void WriteSearcherStageGraph(std::ostream* os);

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

  ClientVerifier* cv_;
  StateMerger* merger_;
  CVSearcher* parent_searcher_;
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
  klee::Mutex lock_;
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
