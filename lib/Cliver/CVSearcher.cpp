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

llvm::cl::opt<unsigned>
StateCacheSize("state-cache-size",llvm::cl::init(100000));

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
  : CVSearcher(NULL, cv, merger) {}

// XXX TBD rework this code
void VerifySearcher::check_searcher_stage_memory() {

  if (cv_->executor()->memory_usage() > (klee::MaxMemory - (klee::MaxMemory/8))) {

    cv_->executor()->update_memory_usage();
    size_t current_usage = cv_->executor()->memory_usage();
    CVMESSAGE("Freeing memory from caches, current usage (MB) " 
              << cv_->executor()->memory_usage());

    foreach (SearcherStage* stage, stages_) {
      size_t cache_size = stage->cache_size();
      if (cache_size > 1) {
        stage->set_capacity(cache_size / 2);
        CVDEBUG("Cache capacity reduced from " 
                << cache_size << " to " << cache_size / 2);
        stage->set_capacity(StateCacheSize);
      }
    }

    cv_->executor()->update_memory_usage();
    CVMESSAGE("Updated usage after freeing caches (MB) "
              << cv_->executor()->memory_usage());

    if (current_usage == cv_->executor()->memory_usage()) {
      cv_error("Freeing caches was not sucessful");
    }
  }
}

klee::ExecutionState &VerifySearcher::selectState() {
  //klee::TimerStatIncrementer timer(stats::searcher_time);
  
  if (!pending_states_.empty()) {
    // Add pending stage to active stage list
    stages_.push_back(get_new_stage(pending_states_.back()));
    pending_states_.pop_back();

    // Compute and update statistics for the previous round
    cv_->set_round(stages_.back()->root_state()->property()->round);

  } else {
    while (!stages_.empty() && stages_.back()->empty()) {
      delete stages_.back();
      stages_.pop_back();
    }
  }

  // Check memory usage
  check_searcher_stage_memory();

  assert(!stages_.empty());

  //return *(static_cast<klee::ExecutionState*>(stages_.back()->next_state()));

  // Check if we should increase k
  CVExecutionState *state = stages_.back()->next_state();

  // Check the edit distance
  if (state->property()->edit_distance == INT_MAX 
      && stages_.back()->size() > 1
      && cv_->execution_trace_manager()->ready_process_all_states(state->property())
      && !stages_.back()->rebuilding()) {
    //CVMESSAGE("Next state is INT_MAX");

    stages_.back()->add_state(state);
    std::vector<ExecutionStateProperty*> states;
    stages_.back()->get_states(states);
    cv_->execution_trace_manager()->process_all_states(states);

    // recompute edit distance
    stages_.back()->set_states(states);
    state = stages_.back()->next_state();
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

  reverse_foreach (SearcherStage* stage, stages_) {
    if (!stage->empty()) return false;
  }

  if (!pending_states_.empty())
    return false;

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
  if (stages_.empty()) {
    stages_.push_back(get_new_stage(state));
  } else {
    if (!check_pending(state))
      stages_.back()->add_state(state);
  }
}

void VerifySearcher::remove_state(CVExecutionState* state) {
  assert(!stages_.empty());
  assert(!check_pending(state));
  stages_.back()->remove_state(state);
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
      case CV_SOCKET_READ: {

        Socket* socket = state->network_manager()->socket();
        ExecutionStateProperty* property = state->property();

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
            CVDEBUG("Removing state at xpilot merge event, wrong round "
                    << *socket << ", State" << *state);

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
  if (!stages_.empty()) 
    stages_.back()->notify(ev);

  switch(ev.event_type) {
    //case CV_SOCKET_SHUTDOWN: {
    //  CVMESSAGE("Socket Shutdown Event: " << *ev.state);
    //  cv_->executor()->setHaltExecution(true);
    //  break;
    //}
    // These events will be processed later
    case CV_FINISH:
    case CV_MERGE:
    case CV_SOCKET_WRITE:
    case CV_SOCKET_READ: {
      pending_events_[ev.state] = ev;
      break;
    }
    default:
      break;
  }
}

////////////////////////////////////////////////////////////////////////////////

TrainingSearcher::TrainingSearcher(ClientVerifier *cv, 
                                         StateMerger* merger)
  : VerifySearcher(cv, merger) {}

klee::ExecutionState &TrainingSearcher::selectState() {
  //klee::TimerStatIncrementer timer(stats::searcher_time);
 
  if (pending_states_.size() > 0 && 
      (stages_.back()->empty() || pending_states_.size() >= TrainingMaxPending)) {
    assert(!pending_states_.empty());

    // Prune state constraints and merge states
    ExecutionStateSet merging_set, state_set;
    merging_set.insert(pending_states_.begin(), pending_states_.end());
    merger_->merge(merging_set, state_set);

    SearcherStageList new_stages;

    foreach (CVExecutionState* state, pending_states_) {
      if (!state_set.count(state)) {
        CVDEBUG("Removing duplicate state " << state << ":" << state->id());
        // Remove/delete states that are duplicates 
        cv_->executor()->remove_state_internal(state);
        ++stats::merged_states;
      } else {
        CVDEBUG("New stage from unique state " << state << ":" << state->id());
        // Create new stage and add to pending list
        new_stages.push_back(get_new_stage(state));
        ++stats::active_states;
      }
    }

    assert(!new_stages.empty()); 

    // Add all pending stages to active stage list
    stages_.insert(stages_.end(), new_stages.begin(), new_stages.end());

    // Compute and update statistics for the previous round
    cv_->set_round(stages_.back()->root_state()->property()->round);

    pending_states_.clear();
  } else {
    while (!stages_.empty() && stages_.back()->empty()) {
      delete stages_.back();
      stages_.pop_back();
    }
  }

  assert(!stages_.empty());
  assert(!stages_.back()->empty());

  check_searcher_stage_memory();

  return *(static_cast<klee::ExecutionState*>(stages_.back()->next_state()));
}

////////////////////////////////////////////////////////////////////////////////



} // end namespace cliver
