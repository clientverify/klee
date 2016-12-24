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

#if defined(USE_BOOST_GRAPHVIZ)
#include <boost/graph/graphviz.hpp>
#endif

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
  // Stage-related functionality
  virtual SearcherStage *
  get_new_stage(CVExecutionState *state); // build new stage from state
  virtual void add_state(CVExecutionState *state);    // used by update()
  virtual void remove_state(CVExecutionState *state); // used by update()

  // Consider the scenario:
  // 1. Worker thread calls selectState()
  // 2. Worker executes state and hits network event, which succeeds
  // 3. Network send event is relayed to the searcher via notify()
  // 4. Worker comes back and returns state to searcher.
  // 5. check_pending() events for the state and process them (e.g., new stage)
  virtual bool check_pending(CVExecutionState *state); // event queue for state
  virtual void process_unique_pending_states();

  virtual void clear_caches(); // (deprecated) Executor calls to free up memory
  virtual SearcherStage *select_stage(); // current searcher stage
  virtual CVExecutionState *check_state_property(
      SearcherStage *,
      CVExecutionState *); // could trigger rebuilding of edit distance tree

  void remove_pending_duplicates(); // unused (remove me)
  bool is_empty();
  SearcherStage *create_and_add_stage(CVExecutionState *state);

#if defined(USE_BOOST_GRAPHVIZ)
  void add_stage_vertex(SearcherStage *s,
                        std::map<SearcherStage *, dot_vertex_desc> &v_map,
                        dot_graph &graph);

  void add_stage_edge(SearcherStage *from, SearcherStage *to, std::string label,
                      std::map<SearcherStage *, dot_vertex_desc> &v_map,
                      dot_graph &graph);
#endif

  ClientVerifier *cv_;

  // Merger has 2 roles: (1) Check equivalence of a set of states and return
  // subset of distinct ones. (2) Prune irrelevant constraints. See RAC thesis
  // for XPilot query size growing over rounds when run w/o pruning.
  StateMerger *merger_;

  CVSearcher *parent_searcher_;
  SearcherStage *current_stage_;
  SearcherStage *last_stage_cleared_;
  unsigned current_round_; // may be less than max_active_round_ (backtracking)
  unsigned max_active_round_;
  bool at_kprefix_max_; // max edit distance reached; switch to BFS
  ExecutionStateProperty *prev_property_; // for debugging?
  bool prev_property_removed_;            // for debugging?

  SearcherStageList stages_; // all stages in Searcher

  // states reaching an endpoint (vector is likely to have only one member)
  std::vector<CVExecutionState *> pending_states_;

  // events generated by worker thread executing state(s)
  std::map<CVExecutionState *, ExecutionEvent> pending_events_;

  // usually length <= 1, but maybe more if multiple pending states
  std::vector<SearcherStageList *> new_stages_;

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
