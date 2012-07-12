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

#include "klee/Internal/Module/InstructionInfoTable.h"

#include "llvm/Support/raw_ostream.h"
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

// Helper for debug output
inline std::ostream &operator<<(std::ostream &os, 
		const klee::KInstruction &ki) {
	std::string str;
	llvm::raw_string_ostream ros(str);
	ros << ki.info->id << ":" << *ki.inst;
	//str.erase(std::remove(str.begin(), str.end(), '\n'), str.end());
	return os << ros.str();
}

////////////////////////////////////////////////////////////////////////////////

CVSearcher::CVSearcher(klee::Searcher* base_searcher, ClientVerifier *cv,
                       StateMerger* merger) 
	: base_searcher_(base_searcher), cv_(cv), merger_(merger) {
}

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
  : CVSearcher(NULL, cv, merger), current_stage_(NULL), current_round_(0) {}

void VerifySearcher::clear_caches() {
  CVMESSAGE("VerifySearcher::clear_caches() starting");

  // Iterate over each rounds set of SearcherStages and reset the caches
  for (unsigned i=0; i<new_stages_.size(); ++i) {
    foreach (SearcherStage* stage, *(new_stages_[i])) {
      size_t cache_size = stage->cache_size();
      if (cache_size > 1) {
        CVMESSAGE("Clearing Searcher stage of size: " << cache_size);
        stage->set_capacity(0);
        stage->set_capacity(StateCacheSize);
      }
    }
  }

  CVMESSAGE("VerifySearcher::clear_caches() finished");
}

void VerifySearcher::process_unique_pending_states() {

  std::set<CVExecutionState*> unique_pending_states(pending_states_.begin(),
                                                    pending_states_.end());

  // Compare withing pending states
  if (pending_states_.size() > 1) {

    // Prune state constraints and merge states
    ExecutionStateSet state_set, merged_set;
    state_set.insert(pending_states_.begin(), pending_states_.end());
    merger_->merge(state_set, merged_set);

    SearcherStageList new_stages;

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

    // If other stages for this round already exist, check if equivalent
    if (new_stages_[pending_round]->size() > 0) {

      ExecutionStateSet state_set, merged_set;

      // Collect the other root states for this round
      foreach (SearcherStage* stage, *(new_stages_[pending_round])) {
        state_set.insert(stage->root_state());
      }

      // Add pending state
      state_set.insert(pending_state);

      // Determine if the states can be "merged" (equivalence check)
      merger_->merge(state_set, merged_set);

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

  // Create new SearcherStageList if needed
  if (round >= new_stages_.size()) {
    new_stages_.push_back(new SearcherStageList());
  }

  // Add pending stage to active stage list
  SearcherStage* new_stage = get_new_stage(state);
  new_stages_[round]->push_back(new_stage);

  return new_stage;
}

klee::ExecutionState &VerifySearcher::selectState() {
  //klee::TimerStatIncrementer timer(stats::searcher_time);

  if (!pending_states_.empty()) {
    process_unique_pending_states();
  }

  // If we've exhausted all the states in the current stage or there is a newer
  // stage to search
  if (current_stage_->empty() ||
      current_round_ < (new_stages_.size()-1)) {

    // Start looking for a non-empty stage in greatest round seen so far
    SearcherStage* new_current_stage = NULL;
    int new_current_round = new_stages_.size() - 1; 

    // Walk backwards through the stages from most recent round to previous
    // rounds
    while (NULL == new_current_stage && new_current_round >= 0) {

      foreach (SearcherStage* stage, *(new_stages_[new_current_round])) {
        if (!stage->empty()) {
          new_current_stage = stage;
          break;
        }
      }

      if (NULL == new_current_stage)
        new_current_round--;
    }

    if (NULL != new_current_stage) {
      current_stage_ = new_current_stage;
      current_round_ = new_current_round;
      cv_->set_round(current_round_);
    }
  }

  if (NULL == current_stage_ || current_stage_->empty()) {
    cv_error("No stages remain!");
  }

  // Check if we should increase k
  CVExecutionState *state = current_stage_->next_state();

  // Check the edit distance
  if (state->property()->edit_distance == INT_MAX 
      && current_stage_->size() > 1
      && cv_->execution_trace_manager()->ready_process_all_states(state->property())
      && !current_stage_->rebuilding()) {
    //CVMESSAGE("Next state is INT_MAX");

    current_stage_->add_state(state);
    std::vector<ExecutionStateProperty*> states;
    current_stage_->get_states(states);
    cv_->execution_trace_manager()->process_all_states(states);

    // recompute edit distance
    current_stage_->set_states(states);
    state = current_stage_->next_state();
  }

  return *(static_cast<klee::ExecutionState*>(state));
}

void VerifySearcher::update(klee::ExecutionState *current,
    const std::set<klee::ExecutionState*> &addedStates,
    const std::set<klee::ExecutionState*> &removedStates) {
  //klee::TimerStatIncrementer timer(stats::searcher_time);

  if (current != NULL && removedStates.count(current) == 0) {
    this->add_state(static_cast<CVExecutionState*>(current));
  }

  if (addedStates.size()) {
    foreach (klee::ExecutionState* klee_state, addedStates) {
     this->add_state(static_cast<CVExecutionState*>(klee_state));
    }
  }

  if (removedStates.size()) {
    foreach (klee::ExecutionState* klee_state, removedStates) {
     this->remove_state(static_cast<CVExecutionState*>(klee_state));
    }
  }
}

bool VerifySearcher::empty() {
  
  //if (ClientModelFlag == XPilot &&
  //    !cv_->executor()->finished_states().empty()) {
  //  CVDEBUG("Exiting. Num finished states: " 
  //          << cv_->executor()->finished_states().size());
  //  return true;
  //}
  
  if (current_stage_ && !current_stage_->empty())
    return false;

  if (!pending_states_.empty())
    return false;

  for (int i = new_stages_.size()-1; i >= 0; --i) {
    foreach (SearcherStage* stage, *(new_stages_[i])) {
      if (!stage->empty()) 
        return false;
    }
  }
 
  //reverse_foreach (SearcherStage* stage, stages_) {
  //  if (!stage->empty()) return false;
  //}

  //if (!pending_states_.empty())
  //  return false;

  return true;
}

SearcherStage* VerifySearcher::get_new_stage(CVExecutionState* state) {
  SearcherStage* stage = SearcherStageFactory::create(merger_, state);
  CVExecutionState* next_state = stage->next_state();
  stage->add_state(next_state);
  cv_->notify_all(ExecutionEvent(CV_SEARCHER_NEW_STAGE, next_state, state));
  return stage;
}

void VerifySearcher::add_state(CVExecutionState* state) {
  if (this->empty()) {
    //stages_.push_back(get_new_stage(state));
    CVMESSAGE("Creating stage from add_state() " << *state);
    current_stage_ = create_and_add_stage(state);
  } else {
    if (!check_pending(state))
      current_stage_->add_state(state);
  }
}

void VerifySearcher::remove_state(CVExecutionState* state) {
  //assert(!this->empty());
  //assert(!check_pending(state));
  current_stage_->remove_state(state);
}

// Checks if there are any pending events to processed associated with
// this state
bool VerifySearcher::check_pending(CVExecutionState* state) {
  bool result = false;
  if (pending_events_.count(state)) {
    switch (pending_events_[state].event_type) {

      case CV_FINISH: {
        if (!state->network_manager()->socket()->is_open()) {
          CVMESSAGE("Finish Event: " << *state);
          cv_->executor()->add_finished_state(state);
          pending_states_.push_back(state);
          result = true;
        } else {
          CVDEBUG("Finish Event (invalid): " << *state);
          cv_->executor()->remove_state_internal(state);
        }
        break;
      }

      case CV_SOCKET_WRITE:
      case CV_SOCKET_READ: 
      case CV_SOCKET_ADVANCE:
      {

        Socket* socket = state->network_manager()->socket();
        ExecutionStateProperty* property = state->property();
        property->round++;

        CVDEBUG("New pending stage. Socket: "
                << *socket << ", State" << *state);

        // Create new stage and add to pending list
        if (ClientModelFlag != XPilot) {
          // XXX Hack to prune state constraints
          ExecutionStateSet state_set, merged_set;
          state_set.insert(state);
          merger_->merge(state_set, merged_set);

          pending_states_.push_back(*(merged_set.begin()));
        } else {
          pending_states_.push_back(state);
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
  if (current_stage_ && !current_stage_->empty()) 
    current_stage_->notify(ev);

  switch(ev.event_type) {
    //case CV_SOCKET_SHUTDOWN: {
    //  CVMESSAGE("Socket Shutdown Event: " << *ev.state);
    //  cv_->executor()->setHaltExecution(true);
    //  break;
    //}
    // These events will be processed later
    case CV_FINISH:
    case CV_MERGE:
    //case CV_SOCKET_WRITE:
    //case CV_SOCKET_READ: 
    case CV_SOCKET_ADVANCE:
    {
      pending_events_[ev.state] = ev;
      break;
    }
    case CV_CLEAR_CACHES: {
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

klee::ExecutionState &TrainingSearcher::selectState() {
  //klee::TimerStatIncrementer timer(stats::searcher_time);
 
  if (pending_states_.size() > 0 && 
      (current_stage_->empty() || pending_states_.size() >= TrainingMaxPending)) {
    process_unique_pending_states();
  }

  // If we've exhausted all the states in the current stage or there is a newer
  // stage to search
  if (current_stage_->empty() ||
      current_round_ < (new_stages_.size()-1)) {

    // Start looking for a non-empty stage in greatest round seen so far
    SearcherStage* new_current_stage = NULL;
    int new_current_round = new_stages_.size() - 1; 

    // Walk backwards through the stages from most recent round to previous
    // rounds
    while (NULL == new_current_stage && new_current_round >= 0) {

      foreach (SearcherStage* stage, *(new_stages_[new_current_round])) {
        if (!stage->empty()) {
          new_current_stage = stage;
          break;
        }
      }

      if (NULL == new_current_stage)
        new_current_round--;
    }

    if (NULL != new_current_stage) {
      current_stage_ = new_current_stage;
      current_round_ = new_current_round;
      cv_->set_round(current_round_);
    }
  }

  if (NULL == current_stage_ || current_stage_->empty()) {
    cv_error("No stages remain!");
  }

  CVExecutionState *state = current_stage_->next_state();

  return *(static_cast<klee::ExecutionState*>(state));
}

////////////////////////////////////////////////////////////////////////////////

} // end namespace cliver
