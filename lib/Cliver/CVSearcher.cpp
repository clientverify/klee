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
    if (cv_->executor()->PauseExecution()) {
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
        cv_->set_round(current_round_);
#ifndef NDEBUG
        if (prev_property_)
          delete prev_property_;
        prev_property_ = NULL;
#endif
        at_kprefix_max_ = false;
      } else {
        cv_error("No stages remain!");
      }

      cv_->executor()->UnPauseExecution();
    } else {
      lock_.lock();
      CVDEBUG("Failed to create new stage, unable to pause execution");
      return NULL;
    }
  }

  return current_stage_;
}

CVExecutionState* VerifySearcher::check_state_property(
    SearcherStage* stage, CVExecutionState* state) {

  if (!state) return NULL;

  CVExecutionState *updated_state = state;

  // should we increase k?
  if (state->property()->edit_distance == INT_MAX 
      && cv_->execution_trace_manager()->ready_process_all_states(state->property())
      && !stage->rebuilding()) {

    lock_.unlock();
    if (cv_->executor()->PauseExecution()) {
      CVMESSAGE("Next state is INT_MAX, Rebuilding, # states = " << stage->size());
      // Add state back and process all states
      stage->add_state(state);
      std::vector<ExecutionStateProperty*> states;
      stage->get_states(states);
      cv_->execution_trace_manager()->process_all_states(states);

      // recompute edit distance
      stage->set_states(states);
      updated_state = stage->next_state();
      cv_->executor()->UnPauseExecution();
    } else {
      CVDEBUG("Rebuild Failure: Next state is INT_MAX, couldn't pause execution");
    }
    lock_.lock();
  }

  return updated_state;
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
    const std::set<klee::ExecutionState*> &addedStates,
    const std::set<klee::ExecutionState*> &removedStates) {
  klee::TimerStatIncrementer timer(stats::searcher_time);

  if (current != NULL && removedStates.count(current) == 0) {
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
    const std::set<klee::ExecutionState*> &addedStates,
    const std::set<klee::ExecutionState*> &removedStates) {
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
    cv_->set_round(0);
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

        // Handle multipass
        if (state->multi_pass_assignment().bindings.size()
            && current_stage_ && current_stage_->root_state()) {

          CVDEBUG("Multi-pass: " << property->pass_count <<
                  " Round: " << current_round_ <<
                  " Instructions: " <<
                  cv_->get_round_statistic_value(current_round_,
                                                 stats::round_instructions) <<
                  " Assignments: " << state->multi_pass_assignment());

          assert(state->multi_pass_clone_ != NULL);

          // Clone ExecutionStateProperty
          ExecutionStateProperty* property_clone = state->multi_pass_clone_->property();

          // Increment pass count
          property_clone->pass_count++;
          ++stats::pass_count;

          // Clone the CVExecutionState at the root of the most recent round
          // (we are re-executing)
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

          // Increment pass_count if this is the last one
          if (property->pass_count != 0)
            ++stats::pass_count;
          // Reset pass count for the new state (root of next stage)
          property->pass_count = 0;
          property->round++;
          pending_states_.push_back(state);
        }

        stats::valid_path_instructions = property->inst_count;

        // Debug: Compute and print constraint variable counts
        if (DebugSearcher) {
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
}

void VerifySearcher::notify(ExecutionEvent ev) {
  // Notify stage for cache events
  // XXX FIXME
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

////////////////////////////////////////////////////////////////////////////////

} // end namespace cliver
