//===-- CVSearcher.cpp ------------------------------------------*- C++ -*-===//
//
// <insert license>
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//

#include "cliver/CVSearcher.h"

#include "cliver/ClientVerifier.h"
#include "cliver/CVExecutionState.h"
#include "cliver/CVExecutor.h"
#include "cliver/ConstraintPruner.h"
#include "cliver/CVStream.h"
#include "cliver/ExecutionTraceManager.h"
#include "cliver/NetworkManager.h"
#include "cliver/StateMerger.h"

#include "klee/util/ExprUtil.h" // Needed for findSymbolicObjects()

#include "CVCommon.h"

#include "llvm/Support/CommandLine.h"

#if defined(USE_BOOST_GRAPHVIZ)
#include <boost/graph/graphviz.hpp>
#endif

namespace klee {
extern llvm::cl::opt<unsigned> MaxMemory;
}

namespace cliver {

llvm::cl::opt<bool>
DebugSearcher("debug-searcher",llvm::cl::init(false));

llvm::cl::opt<size_t>
StateCacheSize("state-cache-size",llvm::cl::init(INT_MAX));

llvm::cl::opt<unsigned>
TrainingMaxPending("training-max-pending",llvm::cl::init(1));

llvm::cl::opt<bool>
LinkFirstPass("link-first-pass",llvm::cl::init(true));

llvm::cl::opt<bool>
FinishAfterLastMessage("finish-after-last-message",llvm::cl::init(false));

////////////////////////////////////////////////////////////////////////////////

#ifndef NDEBUG

#undef CVDEBUG
#define CVDEBUG(x) \
	__CVDEBUG(DebugSearcher, x);

#undef CVDEBUG_S
#define CVDEBUG_S(__state_id, __x) \
	__CVDEBUG_S(DebugSearcher, __state_id, __x)

#else

#undef CVDEBUG
#define CVDEBUG(x)

#undef CVDEBUG_S
#define CVDEBUG_S(__state_id, __x)

#endif

////////////////////////////////////////////////////////////////////////////////

VerifySearcher::VerifySearcher(ClientVerifier* cv, StateMerger* merger)
  : cv_(cv),
    merger_(merger),
    parent_searcher_(NULL),
    current_stage_(NULL), 
    last_stage_cleared_(NULL), 
    current_round_(0),
    max_active_round_(0),
    at_kprefix_max_(false),
    prev_property_(NULL),
    prev_property_removed_(false) {}

void VerifySearcher::clear_caches() {
  CVMESSAGE("VerifySearcher::clear_caches() starting");

  // Iterate over each rounds set of SearcherStages and reset the caches
  for (unsigned i=0; i<new_stages_.size(); ++i) {
    foreach (SearcherStage* stage, *(new_stages_[i])) {
      size_t cache_size = stage->cache_size();
      if (last_stage_cleared_ == current_stage_
					|| stage != current_stage_) {
        if (cache_size > 1) {
          CVMESSAGE("Clearing searcher stage of size: " << cache_size);
          stage->set_capacity(0);
          stage->set_capacity(StateCacheSize);
        }
      } else {
        CVMESSAGE("Not clearing current searcher stage of size: " << cache_size);
      }
    }
  }
	last_stage_cleared_ = current_stage_;

  CVMESSAGE("VerifySearcher::clear_caches() finished");
}

void VerifySearcher::process_unique_pending_states() {

  std::set<CVExecutionState*> unique_pending_states(pending_states_.begin(),
                                                    pending_states_.end());
  // Prune state constraints and merge states
  ExecutionStateSet state_set, merged_set;
  state_set.insert(pending_states_.begin(), pending_states_.end());

  // Don't attempt to merge a single state, just prune constraints
  if (state_set.size() == 1) {
    CVExecutionState *state = *(state_set.begin());
    merger_->pruner()->prune_independent_constraints(*state);
    merged_set.insert(state);
  } else {
    merger_->merge(state_set, merged_set);
  }

  // Check if duplicate pending states exist
  if (pending_states_.size() > 1) {

    // Check if a pending state is not found in merged set, if so remove as a
    // duplicate
    foreach (CVExecutionState* state, pending_states_) {
      if (!merged_set.count(state)) {
        CVDEBUG("Removing duplicate state " << state << ":" << state->id());
        // Remove/delete states that are duplicates 
        cv_->executor()->remove_state_internal(state);
        unique_pending_states.erase(state);
        ++stats::merged_states;
      }
    }
  }

  foreach (CVExecutionState* pending_state, unique_pending_states) {
    int pending_round = pending_state->property()->round;

    // Create new SearcherStageList for this round if needed
    assert(pending_round <= new_stages_.size());
    if (pending_round == new_stages_.size()) {
      new_stages_.push_back(new SearcherStageList());
    }
    
    //CVMESSAGE("adding pending state: " << *pending_state);
    //CVMESSAGE("adding pending state. current round: "
    //         << current_round_ << ", pendingstate round: "
    //         << pending_round << ", pending state: " << *pending_state);
    //create_and_add_stage(pending_state);

    // XXX root state not supported
    // If other stages for this round already exist, check if equivalent
    if (new_stages_[pending_round]->size() > 0) {

      ExecutionStateSet state_set, merged_set;

      // Collect the other root states for this round
      foreach (SearcherStage* stage, *(new_stages_[pending_round])) {
        state_set.insert(stage->root_state());
        CVDEBUG("Attempting to merge existing state: " << stage->root_state());
        merged_set.insert(stage->root_state());
      }

      // Add pending state
      state_set.insert(pending_state);
      merged_set.insert(pending_state);

      // Determine if the states can be "merged" (equivalence check)
      // FIXME: Disabled merging
      //merger_->merge(state_set, merged_set);

      if (merged_set.size() != state_set.size()) {
        assert((merged_set.size() + 1) == state_set.size());
        ++stats::merged_states;
        cv_->executor()->remove_state_internal(pending_state);
      } else {
        create_and_add_stage(pending_state);
      }
    } else {
      create_and_add_stage(pending_state);
    }
  }
  pending_states_.clear();
}

SearcherStage* VerifySearcher::create_and_add_stage(CVExecutionState* state) {

  // Extract round number
  int round = std::max(state->property()->round,0);

  // Update maximum active round
  max_active_round_ = std::max((int)max_active_round_, round);

  // Create new SearcherStageList if needed
  if (round >= new_stages_.size()) {
    new_stages_.push_back(new SearcherStageList());
  }

  // Add pending stage to active stage list
  SearcherStage* new_stage = get_new_stage(state);
  new_stages_[round]->push_back(new_stage);

  return new_stage;
}

SearcherStage* VerifySearcher::select_stage() {
  assert(0);
  return NULL;
#if 0
  bool pending_states_processed = false;
  if (!pending_states_.empty()) {
    process_unique_pending_states();
    pending_states_processed = true;
  }

  // If we've exhausted all the states in the current stage or there is a newer
  // stage to search
  if (current_stage_->empty() ||
      current_round_ < max_active_round_ ||
      pending_states_processed) {

    lock_.unlock();
      if (parent_searcher_ != NULL)
        parent_searcher_->flush();

      lock_.lock();

      // Start looking for a non-empty stage in greatest round seen so far
      SearcherStage* new_current_stage = NULL;
      int new_current_round = max_active_round_;

      // Walk backwards through the stages from most recent round to previous
      // rounds
      while (NULL == new_current_stage && new_current_round >= 0) {

        // Walk backwards to prioritize most recent stages for this round
        reverse_foreach (SearcherStage* stage, *(new_stages_[new_current_round])) {
          if (!stage->empty()) {
            new_current_stage = stage;
            CVDEBUG("New Stage: Round: " << new_current_round <<
                    " State: " << *(stage->root_state()) <<
                    " Assignments: " <<
                    stage->root_state()->multi_pass_assignment());
            break;
          }
        }

        if (NULL == new_current_stage) {
          // All (if any) stages in this round our empty, continue searching
          // backwards in the stage history for a state that is ready to execute
          new_current_round--;

          // Decrement max_active_round, so that we don' repeat this effort
          // on the next instruction
          max_active_round_ = new_current_round;
        }
      }

      if (NULL != new_current_stage) {
        current_stage_ = new_current_stage;
        current_round_ = new_current_round;
        cv_->set_round(current_round_, current_stage_);
#ifndef NDEBUG
        if (prev_property_)
          delete prev_property_;
        prev_property_ = NULL;
#endif
        at_kprefix_max_ = false;
      } else {
        cv_error("No stages remain!");
      }

  }

  return current_stage_;
#endif
}

CVExecutionState* VerifySearcher::check_state_property(
    SearcherStage* stage, CVExecutionState* state) {
  assert(0);
  return NULL;
#if 0
  if (!state) return NULL;

  CVExecutionState *updated_state = state;

  // should we increase k?
  if (state->property()->edit_distance == INT_MAX 
      && cv_->execution_trace_manager()->ready_process_all_states(state->property())
      && !stage->rebuilding()) {

    lock_.unlock();
      CVMESSAGE("Next state is INT_MAX, Rebuilding, # states = " << stage->size());
      // Add state back and process all states
      stage->add_state(state);
      std::vector<ExecutionStateProperty*> states;
      stage->get_states(states);
      cv_->execution_trace_manager()->process_all_states(states);

      // recompute edit distance
      stage->set_states(states);
      updated_state = stage->next_state();
    lock_.lock();
  }

  return updated_state;
#endif
}

klee::ExecutionState* VerifySearcher::trySelectState() {
  klee::LockGuard guard(lock_);

  CVExecutionState *state = NULL;

  if (!is_empty()) {
    SearcherStage* stage = select_stage();
    if (stage) {
      state = current_stage_->next_state();
      state = check_state_property(current_stage_, state);
    }
  }
  return state;
}

klee::ExecutionState &VerifySearcher::selectState() {
  klee::LockGuard guard(lock_);

  CVExecutionState *state = NULL;

  current_stage_ = select_stage();

  assert(current_stage_);

  state = current_stage_->next_state();

  assert(state);

  state = check_state_property(current_stage_, state);

#ifndef NDEBUG
  if (!at_kprefix_max_ && state->property()->edit_distance == INT_MAX 
      && !cv_->execution_trace_manager()->ready_process_all_states(state->property())) {
    at_kprefix_max_ = true;
    CVDEBUG("Switching to BFS, KPrefix based search is exhausted.");
  }

  // Sanity checks for heap operation
  if (prev_property_) {
    bool failed_check = false;
    if (prev_property_->is_recv_processing != state->property()->is_recv_processing) {
      if (state->property()->is_recv_processing) {
        CVDEBUG("Switched to recv processing: " << *(state->property()));
      } else {
        CVDEBUG("Switched from recv processing: " << *(state->property()));
      }
    } else if (!prev_property_removed_ && !(state->property()->is_recv_processing)) {
      if (prev_property_->edit_distance > state->property()->edit_distance)
        failed_check = true;

      if (state->property()->edit_distance == INT_MAX
        && prev_property_->symbolic_vars > state->property()->symbolic_vars)
          failed_check = true;

      if (failed_check) {
        CVDEBUG("Searcher Property Check Failed: " 
                  << *prev_property_ << ", " << *(state->property()));
      }

      //assert(prev_property_->edit_distance <= state->property()->edit_distance);
      //assert(state->property()->edit_distance != INT_MAX
      //       || (prev_property_->symbolic_vars <= state->property()->symbolic_vars));

    }

    if (prev_property_->client_round != state->property()->client_round)
      CVDEBUG("Client round changed to " << state->property()->client_round);

    if (prev_property_->symbolic_vars != state->property()->symbolic_vars)
      CVDEBUG("Symbolic vars changed to " << state->property()->symbolic_vars);

    prev_property_removed_ = false;
  } 

  if (!prev_property_)
    prev_property_ = state->property()->clone();
  else 
    *prev_property_ = *(state->property());
#endif

  return *(static_cast<klee::ExecutionState*>(state));
}

void VerifySearcher::update(klee::ExecutionState *current,
    const std::vector<klee::ExecutionState *> &addedStates,
    const std::vector<klee::ExecutionState *> &removedStates) {
  klee::TimerStatIncrementer timer(stats::searcher_time);

  if (current != NULL && (std::find(removedStates.begin(), removedStates.end(),
                                    current) == removedStates.end())) {
    klee::LockGuard guard(lock_);
    this->add_state(static_cast<CVExecutionState*>(current));
  }

  if (addedStates.size()) {
    klee::LockGuard guard(lock_);
    foreach (klee::ExecutionState* klee_state, addedStates) {
      this->add_state(static_cast<CVExecutionState*>(klee_state));
    }
  }

  if (removedStates.size()) {
    klee::LockGuard guard(lock_);
    foreach (klee::ExecutionState* klee_state, removedStates) {
      this->remove_state(static_cast<CVExecutionState*>(klee_state));
    }
  }
}

klee::ExecutionState* VerifySearcher::updateAndTrySelectState(
    klee::ExecutionState *current,
    const std::vector<klee::ExecutionState *> &addedStates,
    const std::vector<klee::ExecutionState *> &removedStates) {
  update(current, addedStates, removedStates);
  return trySelectState();
}
bool VerifySearcher::empty() {
  klee::LockGuard guard(lock_);
  return is_empty();
}

bool VerifySearcher::is_empty() {

  if (current_stage_ && current_stage_->size() > 0)
    return false;

  if (!pending_states_.empty())
    return false;

  ////XXX TODO THREAD SAFE BACKTRACKING
  if (current_stage_ && current_stage_->size() == 0 &&
      pending_states_.empty() &&
      current_stage_->live_count() == 0) {
    for (int i = new_stages_.size()-1; i >= 0; --i) {
      foreach (SearcherStage* stage, *(new_stages_[i])) {
        if (!stage->empty())
          return false;
      }
    }
  }

  CVDEBUG("VerifySearcher is empty!");
  return true;
}
SearcherStage* VerifySearcher::get_new_stage(CVExecutionState* state) {
  SearcherStage* stage = SearcherStageFactory::create(merger_, state);
  CVExecutionState* next_state = stage->next_state();

  if (state != NULL && state->searcher_stage() != NULL) {
    state->searcher_stage()->leaf_stages.push_back(stage);
  }

  // Common case: parent state is a leaf node of the current stage
  CVExecutionState* parent_state = state;

  if (current_stage_) {
    // During multipass execution, the parent stage
    // is the stage from the previous round, and the
    // multipass parent is the previous multi-pass stage
    if (state->multi_pass_assignment().size()) {
      stage->multi_pass_parent = current_stage_;
      SearcherStage* lookup_stage = current_stage_;
      while (lookup_stage->multi_pass_parent != NULL) {
        lookup_stage = lookup_stage->multi_pass_parent;
      }
      stage->parent = lookup_stage->parent;
      parent_state = stage->multi_pass_parent->root_state();
    } else {
      stage->parent = current_stage_;
    }

    // If LinkFirstPass is enabled, and we just finished a
    // multipass stage, we notify ExecutionTreeManager
    // that the parent of the current stage is the first multipass
    // stage, otherwise it is the last
    if (LinkFirstPass &&
        state->multi_pass_assignment().size() == 0 &&
        current_stage_->multi_pass_parent) {

      SearcherStage* lookup_stage = current_stage_->multi_pass_parent;
      while (lookup_stage->multi_pass_parent != NULL) {
        lookup_stage = lookup_stage->multi_pass_parent;
      }

      stage->parent = lookup_stage;

      parent_state = stage->parent->leaf_states.back();

      // Increment round for an early pass
      parent_state->property()->round++;
    }
  }

  // Notify
  //lock_.unlock();
  cv_->notify_all(ExecutionEvent(CV_SEARCHER_NEW_STAGE, next_state, parent_state));
  //lock_.lock();

  // Reset property values
  next_state->property()->reset();

  // Add back to stage
  stage->add_state(next_state);

  return stage;
}

void VerifySearcher::add_state(CVExecutionState* state) {
  if (current_stage_ == NULL) {
    CVMESSAGE("Creating stage from add_state() " << *state);
    current_stage_ = create_and_add_stage(state);
    cv_->set_round(0, current_stage_);
  } else {
    if (!check_pending(state))
      current_stage_->add_state(state);
  }
}

void VerifySearcher::remove_state(CVExecutionState* state) {
#ifndef NDEBUG
  if (prev_property_ == state->property()) {
    prev_property_removed_ = true;
  }
#endif

  current_stage_->remove_state(state);
}

// Checks if there are any pending events to processed associated with
// this state
bool VerifySearcher::check_pending(CVExecutionState* state) {
  assert(0);
  return false;
#if 0
  bool result = false;
  if (pending_events_.count(state)) {
    klee::TimerStatIncrementer timer(stats::searcher_time);
    switch (pending_events_[state].event_type) {

      // TODO: this logic should completely moved to CVExecutor
      // (see terminateStateOnExit)
      case CV_FINISH: {
        if (!state->network_manager()->socket()->is_open()) {
          ExecutionStateProperty* property = state->property();
          property->round++;
          CVMESSAGE("Finish Event - Socket: " << *(state->network_manager()->socket())<< " state: " << *state);
          cv_->executor()->add_finished_state(state);
          pending_states_.push_back(state);
          result = true;
        } else {
          CVDEBUG("Finish Event (invalid): " << *state);
          cv_->executor()->terminate_state(state);
          result = true;
        }
        break;
      }

      case CV_SOCKET_ADVANCE: {

        ExecutionStateProperty* property = state->property();
        Socket* socket = state->network_manager()->socket();

        // Record successful leaf state in stage
        current_stage_->leaf_states.push_back(state);

        // Handle multipass: "bindings" maps variable names to concrete values
        if (state->multi_pass_assignment().bindings.size() && current_stage_ &&
            current_stage_->root_state() &&
            state->multi_pass_assignment().bindings !=
                state->multi_pass_bindings_prev()) {

          // FIXME: do we need to do a deeper inequality check of the
          // bindings in the if() condition above, rather than simply
          // comparing the bindings using the operator!= for std::map?

          CVDEBUG("Multi-pass: " << property->pass_count <<
                  " Round: " << current_round_ <<
                  " Instructions: " <<
                  cv_->get_round_statistic_value(current_round_,
                                                 stats::round_instructions) <<
                  " Assignments: " << state->multi_pass_assignment());

          if (state->multi_pass_clone_ == NULL) {
            // This means there were no symbolic variables generated
            // during this round. In this case, set multi_pass_clone_
            // to the stage's root state (i.e., unoptimized multipass
            // behavior), instead of the state when the first symbolic
            // variable is about to be generated.
            ExecutionStateProperty *property =
                current_stage_->root_state()->property()->clone();
            state->multi_pass_clone_ =
                current_stage_->root_state()->clone(property);
          }

          // Save the assignments as the "previous multipass bindings"
          // in case we execute another round but learn nothing new.
          state->multi_pass_clone_->set_multi_pass_bindings_prev(
              state->multi_pass_assignment().bindings);

          // Clone ExecutionStateProperty
          ExecutionStateProperty* property_clone = state->multi_pass_clone_->property();

          // Increment pass count
          property_clone->pass_count++;
          stats::pass_count = property_clone->pass_count;

          // Select the clone of the CVExecutionState at the root of
          // the most recent round, or (as an optimization) the clone
          // of the execution state at which the first symbolic
          // variable appeared in this round.
          CVExecutionState* new_state = state->multi_pass_clone_;

          state->multi_pass_clone_ = NULL;

          // Provide cloned root state with constraints and assignments
          // concretized in the most recent pass
          new_state->set_multi_pass_assignment( state->multi_pass_assignment());

          // Add cloned root state to pending states list
          pending_states_.push_back(new_state);

        } else {

          if (socket->previous_event().type != SocketEvent::RECV)
            property->is_recv_processing = false;

          // Set pass count if this round was (last) multipass round
          if (property->pass_count != 0) {
            stats::pass_count = property->pass_count;
          }
          // Reset pass count for the new state (root of next stage)
          property->pass_count = 0;
          property->round++;
          // Reset multipass bindings (if any)
          state->multi_pass_bindings_prev().clear();

          pending_states_.push_back(state);
        }

        stats::valid_path_instructions = property->inst_count;

        // Debug: Compute and print constraint variable counts
        if (DebugSearcher) {
#if 0
          std::map<std::string,int> constraint_map;
          foreach (klee::ref<klee::Expr> e, state->constraints) {
            std::vector<const klee::Array*> arrays;
            klee::findSymbolicObjects(e, arrays);
            for (unsigned i=0; i<arrays.size(); ++i) {
              constraint_map[arrays[i]->name]++;
            }
          }
          typedef std::map<std::string,int> constraint_map_ty;

          std::stringstream ss;
          ss << "Debug: Constraint variables (" << constraint_map.size() << "): ";
          foreach (constraint_map_ty::value_type v, constraint_map) {
            ss << v.first << ":" << v.second << ", ";
          }
          CVDEBUG(ss.str());
#endif
        }

        if (FinishAfterLastMessage) {
          if (state->network_manager()->socket()->end_of_log())  {
            ExecutionStateProperty* property = state->property();
            CVMESSAGE("Finish Event - Last Message - Socket: " 
                      << *(state->network_manager()->socket())
                      << " state: " << *state);
            cv_->executor()->add_finished_state(state);
          }
        }

        result = true;
        break;
      }
      case CV_MERGE: {

        Socket* socket = state->network_manager()->socket();
        ExecutionStateProperty* property = state->property();

        if (ClientModelFlag == XPilot) {
          if (socket->client_round() <= property->client_round) {
            CVDEBUG("Removing state at xpilot merge event, wrong round. Socket: "
                    << *socket << ", State: " << *state);

            // Remove from set
            pending_events_.erase(state);

            // Remove State from current stage
            this->remove_state(state);
            
            // Remove invalid state with unfinished network processing
            cv_->executor()->remove_state_internal(state);

            return true;

          } else {
            CVDEBUG("Incrementing xpilot client round ");
            property->client_round++;
          }
        }
        break;
      }
      default:
        break;
    }

    // Remove from set
    pending_events_.erase(state);

    // Remove State from current stage
    this->remove_state(state);

  }
  return result;
#endif
}

void VerifySearcher::notify(ExecutionEvent ev) {
  // Notify stage for cache events
  // XXX FIXME // only needed for state rebuilder, recording every branch
  //if (current_stage_ && !current_stage_->empty())
  //  current_stage_->notify(ev);

  switch(ev.event_type) {
    // These events will be processed later
    case CV_FINISH:
    case CV_MERGE:
    case CV_SOCKET_ADVANCE: {
      klee::LockGuard guard(lock_);
      pending_events_[ev.state] = ev;
      break;
    }
    case CV_CLEAR_CACHES: {
      cv_error("CLEAR_CACHES not currently supported!");
      if (!current_stage_->rebuilding()) {
        clear_caches();
      }
      break;
    }
    default:
      break;
  }
}

#if defined(USE_BOOST_GRAPHVIZ)
void VerifySearcher::add_stage_vertex(SearcherStage *s,
                      std::map<SearcherStage *, dot_vertex_desc> &v_map,
                      dot_graph &graph) {
  if (v_map.count(s) == 0) {

    //*cv_message_stream << "STAGE " << s;
    //cv_->sm()->print_stage(*cv_message_stream,s, " ");
    std::stringstream ss;
    std::string v_name;
    uint64_t tm = cv_->sm()->get_stage_statistic(s, "RoundRealTime");
    uint64_t rn = cv_->sm()->get_stage_statistic(s, "RoundNumber");
    uint64_t scc = cv_->sm()->get_stage_statistic(s, "StateCloneCount");
    uint64_t src = cv_->sm()->get_stage_statistic(s, "StateRemoveCount");
    uint64_t vpi =
        cv_->sm()->get_stage_statistic(s, "ValidPathInstructionCount");
    uint64_t ic = cv_->sm()->get_stage_statistic(s, "InstructionCount");

    double dtm = ((double)tm) / ((double)1000000.0);

    ss << "Rn:" << rn <<
        "\nTm:" << dtm <<
        "\nIC:" << ic <<
        "\nSC:" << scc <<
        "\nSR:" << src <<
        "\nVPI:" << vpi;

    v_name = ss.str();

    dot_vertex v(v_name);

    v_map[s] = boost::add_vertex(v, graph);
  }
}

void VerifySearcher::add_stage_edge(SearcherStage *from, SearcherStage *to,
                    std::string label,
                    std::map<SearcherStage *, dot_vertex_desc> &v_map,
                    dot_graph &graph) {
    add_stage_vertex(from, v_map, graph);
    add_stage_vertex(to, v_map, graph);
    dot_edge edge(label);
    boost::add_edge(v_map[from], v_map[to], edge, graph);
}
#endif

void VerifySearcher::WriteSearcherStageGraph(std::ostream *os) {

#if defined(USE_BOOST_GRAPHVIZ)
  CVMESSAGE("Generating SearcherStage graph file");
  SearcherStage *root = SearcherStageFactory::create(
      merger_, new_stages_[0]->front()->root_state());

  for (auto stage : *(new_stages_[0])) {
    //CVMESSAGE("Root Stage: " << stage << " " << stage->root_state());
    root->leaf_stages.push_back(stage);
  }

  std::map<SearcherStage *, dot_vertex_desc> v_map;
  std::set<std::pair<SearcherStage*, SearcherStage*> > edge_set;
  std::set<SearcherStage*> stage_set;

  dot_graph graph;
  std::stack<SearcherStage *> worklist;
  worklist.push(root);
  while (!worklist.empty()) {

    SearcherStage *parent = worklist.top();
    worklist.pop();
    stage_set.insert(parent);

    add_stage_vertex(parent, v_map, graph);

    //CVMESSAGE("Parent: " << parent << " " << parent->root_state());
    //for (auto child : parent->leaf_stages) {
    //  CVMESSAGE("\tChild: " << child << " " << child->root_state());
    //}

    for (auto child : parent->leaf_stages) {
      //CVMESSAGE("Stage: " << child << " " << child->root_state());

      auto pair = std::make_pair(parent, child);
      if (edge_set.count(pair) == 0) {
        std::stringstream ss;
        int pass_count = child->root_state()->property()->pass_count;
        ss << "Pass=" << pass_count;
        ss << "\nICnt=" << child->root_state()->property()->inst_count;
        ss << "\nSVars=" << child->root_state()->property()->symbolic_vars;
        if (pass_count > 0) {
          ss << "\nVars=" << child->root_state()->multi_pass_assignment();
        }
        add_stage_edge(pair.first, pair.second, ss.str(), v_map, graph);
        worklist.push(child);
        edge_set.insert(pair);
      }

      if (child->multi_pass_parent != NULL) {
        if (child->parent) {
          add_stage_edge(child->parent, child,
                        "MultiPass", v_map, graph);
        }

      }
    }
  }

  boost::write_graphviz(
      *os, graph,
      boost::make_label_writer(boost::get(&dot_vertex::name, graph)),
      boost::make_label_writer(boost::get(&dot_edge::name, graph)));
#else
  CVMESSAGE("boost::graphviz disabled, not generating SearcherStage graph file");
#endif
}

////////////////////////////////////////////////////////////////////////////////

} // end namespace cliver
