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
#include "cliver/CVStream.h"
#include "cliver/ExecutionTraceManager.h"
#include "cliver/NetworkManager.h"
#include "cliver/StateMerger.h"

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

CVSearcher::CVSearcher(klee::Searcher* base_searcher, ClientVerifier *cv,
                       StateMerger* merger) 
	: base_searcher_(base_searcher), cv_(cv), merger_(merger) { }

klee::ExecutionState &CVSearcher::selectState() {
	//klee::TimerStatIncrementer timer(stats::searcher_time);
	return base_searcher_->selectState();
}

void CVSearcher::update(klee::ExecutionState *current,
						const std::set<klee::ExecutionState*> &addedStates,
						const std::set<klee::ExecutionState*> &removedStates) {
	//klee::TimerStatIncrementer timer(stats::searcher_time);
	base_searcher_->update(current, addedStates, removedStates);
}

bool CVSearcher::empty() {
	return base_searcher_->empty();
}

////////////////////////////////////////////////////////////////////////////////

VerifySearcher::VerifySearcher(ClientVerifier* cv, StateMerger* merger)
  : CVSearcher(NULL, cv, merger), 
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
  merger_->merge(state_set, merged_set);

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
  if (!pending_states_.empty()) {
    process_unique_pending_states();
  }

  // If we've exhausted all the states in the current stage or there is a newer
  // stage to search
  if (current_stage_->empty() ||
      current_round_ < max_active_round_) {

    lock_.unlock();
    if (cv_->executor()->PauseExecution()) {
      lock_.lock();
      // Start looking for a non-empty stage in greatest round seen so far
      SearcherStage* new_current_stage = NULL;
      int new_current_round = max_active_round_;

      // Walk backwards through the stages from most recent round to previous
      // rounds
      while (NULL == new_current_stage && new_current_round >= 0) {

        foreach (SearcherStage* stage, *(new_stages_[new_current_round])) {
          if (!stage->empty()) {
            new_current_stage = stage;
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

  if ((current_stage_ && current_stage_->size()) || pending_states_.size()) {
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

bool VerifySearcher::empty() {
  
  if (current_stage_ && current_stage_->size() > 0)
    return false;

  if (!pending_states_.empty())
    return false;

  //XXX TODO THREAD SAFE BACKTRACKING
  //for (int i = new_stages_.size()-1; i >= 0; --i) {
  //  foreach (SearcherStage* stage, *(new_stages_[i])) {
  //    if (!stage->empty())
  //      return false;
  //  }
  //}

  CVDEBUG("VerifySearcher is empty!");
  return true;
}

SearcherStage* VerifySearcher::get_new_stage(CVExecutionState* state) {
  SearcherStage* stage = SearcherStageFactory::create(merger_, state);
  CVExecutionState* next_state = stage->next_state();

  // Notify
  lock_.unlock();
  cv_->notify_all(ExecutionEvent(CV_SEARCHER_NEW_STAGE, next_state, state));
  lock_.lock();

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

        if (socket->previous_event().type != SocketEvent::RECV)
          property->is_recv_processing = false;

        property->round++;
        pending_states_.push_back(state);

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

            // Remove invalid state with unfinished network processing
            cv_->executor()->remove_state_internal(state);

            result = true;
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

TrainingSearcher::TrainingSearcher(ClientVerifier *cv, StateMerger* merger)
  : VerifySearcher(cv, merger) {}

klee::ExecutionState* TrainingSearcher::trySelectState() {
  //klee::TimerStatIncrementer timer(stats::searcher_time);
  klee::LockGuard guard(lock_);

  CVExecutionState *state = NULL;

  if ((current_stage_ && current_stage_->size()) || pending_states_.size()) {
    SearcherStage* stage = select_stage();
    if (stage) {
      state = current_stage_->next_state();
    }
  }
  return state;
}

klee::ExecutionState &TrainingSearcher::selectState() {
  //klee::TimerStatIncrementer timer(stats::searcher_time);
  klee::LockGuard guard(lock_);
 
  current_stage_ = select_stage();

  CVExecutionState *state = current_stage_->next_state();

  return *(static_cast<klee::ExecutionState*>(state));
}

SearcherStage* TrainingSearcher::select_stage() {

  if (pending_states_.size() > 0 && 
      (current_stage_->empty() || pending_states_.size() >= TrainingMaxPending)) {
    process_unique_pending_states();
  }

  // If we've exhausted all the states in the current stage or there is a newer
  // stage to search
  if (current_stage_->empty() ||
      current_round_ < max_active_round_) {

    lock_.unlock();
    if (cv_->executor()->PauseExecution()) {
      lock_.lock();

      // Start looking for a non-empty stage in greatest round seen so far
      SearcherStage* new_current_stage = NULL;

      int new_current_round = max_active_round_;

      // Walk backwards through the stages from most recent round to previous
      // rounds
      while (NULL == new_current_stage && new_current_round >= 0) {

        foreach (SearcherStage* stage, *(new_stages_[new_current_round])) {
          if (!stage->empty()) {
            new_current_stage = stage;
            break;
          } else {
            CVDEBUG("Stage Empty, round=" << new_current_round
                    << ", rootstate: " << *(stage->root_state()));
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

////////////////////////////////////////////////////////////////////////////////

} // end namespace cliver
