//===-- CVSearcher.cpp ------------------------------------------*- C++ -*-===//
//
// <insert license>
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//

#include "cliver/CVSearcher.h"
#include "cliver/CVExecutionState.h"
#include "cliver/CVExecutor.h"
#include "cliver/CVStream.h"
#include "cliver/ExecutionTree.h"
#include "cliver/StateMerger.h"
#include "cliver/ClientVerifier.h"
#include "cliver/NetworkManager.h"
#include "CVCommon.h"

#include "klee/Internal/Module/InstructionInfoTable.h"

#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/CommandLine.h"

namespace cliver {

llvm::cl::opt<bool>
DebugSearcher("debug-searcher",llvm::cl::init(false));

llvm::cl::opt<bool>
DeleteOldStates("delete-old-states",llvm::cl::init(true));

llvm::cl::opt<bool>
BacktrackSearching("backtrack-searching",llvm::cl::init(false));

llvm::cl::opt<unsigned>
StateCacheSize("state-cache-size",llvm::cl::init(100000));

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

klee::ExecutionState &VerifySearcher::selectState() {
  //klee::TimerStatIncrementer timer(stats::searcher_time);
  
  if (!pending_stages_.empty()) {
    // Delete all previous states from this round.
    if (DeleteOldStates) {
      stages_.back()->clear();
    }
    // Compute and output statistics for the previous round
    cv_->next_round();

    // Add pending stage to active stage list
    stages_.push_back(pending_stages_.back());
    pending_stages_.pop_back();
  }

  if (BacktrackSearching) {
    while (!stages_.empty() && stages_.back()->empty()) {
      delete stages_.back();
      stages_.pop_back();
    }
  }

  assert(!stages_.empty());

  return *(static_cast<klee::ExecutionState*>(stages_.back()->next_state()));
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
  
  if (ClientModelFlag == XPilot &&
      !cv_->executor()->finished_states().empty()) {
    CVDEBUG("Exiting. Num finished states: " 
            << cv_->executor()->finished_states().size());
    return true;
  }

  if (BacktrackSearching) {
    reverse_foreach (SearcherStage* stage, stages_)
      if (!stage->empty()) return false;
  } else {
    if (!stages_.back()->empty()) 
      return false;
  }

  reverse_foreach (SearcherStage* stage, pending_stages_)
    if (!stage->empty()) return false;

  if (!pending_states_.empty())
    return false;

  return true;
}

SearcherStage* VerifySearcher::get_new_stage(CVExecutionState* state) {
  return SearcherStageFactory::create(merger_, state);
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

bool VerifySearcher::check_pending(CVExecutionState* state) {
  if (pending_states_.count(state)) {
    // Should only be 1 for now.
    assert(pending_states_.size() == 1);

    // Remove from set
    pending_states_.erase(state);

    // Remove State from current stage
    this->remove_state(state);

    if (ClientModelFlag == XPilot && 
        state->network_manager()->socket()->round() <= cv_->round()) {
      CVDEBUG("Removing state at merge event, wront round "
              << *(state->network_manager()->socket()) << ", State" << *state);

      // Remove invalid state with unfinished network processing
      cv_->executor()->remove_state_internal(state);

    } else {
      CVDEBUG("New pending stage. Socket: "
              << *(state->network_manager()->socket()) << ", State" << *state);

      // Create new stage and add to pending list
      if (ClientModelFlag != XPilot) {
        // XXX Hack to prune state constraints
        ExecutionStateSet state_set, merged_set;
        state_set.insert(state);
        merger_->merge(state_set, merged_set);

        pending_stages_.push_back(get_new_stage(*(merged_set.begin())));
      } else {
        pending_stages_.push_back(get_new_stage(state));
      }

    }
    return true;
  }
  return false;
}

void VerifySearcher::notify(ExecutionEvent ev) {
  stages_.back()->notify(ev);

  switch(ev.event_type) {
    case CV_MERGE: {
      CVDEBUG("Merge event " << *ev.state);
      pending_states_.insert(ev.state);
      break;
    }
    case CV_FINISH: {
      CVDEBUG("Finish event " << *ev.state);
      if (!ev.state->network_manager()->socket()->is_open()) {
        CVMESSAGE("Finished state: " << *ev.state);
        cv_->executor()->add_finished_state(ev.state);
      }
      break;
    }
    case CV_SOCKET_WRITE:
    case CV_SOCKET_READ: {
      if (ClientModelFlag != XPilot)
        pending_states_.insert(ev.state);
      break;
    }
    case CV_SOCKET_SHUTDOWN: {
      cv_->executor()->setHaltExecution(true);
      break;
    }
    default:
      break;
  }
}
////////////////////////////////////////////////////////////////////////////////

KExtensionVerifySearcher::KExtensionVerifySearcher(ClientVerifier* cv, 
                                                   StateMerger* merger)
  : VerifySearcher(cv, merger) {}


klee::ExecutionState &KExtensionVerifySearcher::selectState() {
  //klee::TimerStatIncrementer timer(stats::searcher_time);
  
  if (!pending_stages_.empty()) {
    // Delete all previous states from this round.
    if (DeleteOldStates) {
      stages_.back()->clear();
    }
    // Compute and output statistics for the previous round
    cv_->next_round();

    // Add pending stage to active stage list
    stages_.push_back(pending_stages_.back());
    pending_stages_.pop_back();
  }

  if (BacktrackSearching) {
    while (!stages_.empty() && stages_.back()->empty()) {
      delete stages_.back();
      stages_.pop_back();
    }
  }

  assert(!stages_.empty());

  // Check if we should increase k
  CVExecutionState *state = stages_.back()->next_state();

  if (state->property()->edit_distance == INT_MAX 
      && stages_.back()->size() > 1
      && cv_->execution_tree_manager()->ready_process_all_states()
      && !stages_.back()->rebuilding()) {
    //CVMESSAGE("Next state is INT_MAX");

    stages_.back()->add_state(state);
    std::vector<ExecutionStateProperty*> states;
    stages_.back()->get_states(states);
    cv_->execution_tree_manager()->process_all_states(states);
    // recompute edit distance
    stages_.back()->set_states(states);
    state = stages_.back()->next_state();
  }

  return *(static_cast<klee::ExecutionState*>(state));
}

////////////////////////////////////////////////////////////////////////////////

// TODO: Round Robin Training Searcher
// 
// Use state->property()->round instead of cv_->round() when comparing socket
// rounds for xpilot

////////////////////////////////////////////////////////////////////////////////

TrainingSearcher::TrainingSearcher(ClientVerifier *cv, 
                                         StateMerger* merger)
  : VerifySearcher(cv, merger) {}

klee::ExecutionState &TrainingSearcher::selectState() {
  //klee::TimerStatIncrementer timer(stats::searcher_time);
 
  while (!stages_.empty() && stages_.back()->empty()) {
    delete stages_.back();
    stages_.pop_back();
  }
 
  if (stages_.empty()) {
    assert(!pending_states_.empty());

    ExecutionStateSet merging_set, state_set;
    if (ClientModelFlag != XPilot) {
      ExecutionStateSet merging_set, state_set;
      // Prune state constraints and merge states
      merging_set.insert(pending_states_.begin(), pending_states_.end());
      merger_->merge(merging_set, state_set);
    } else {
      state_set.insert(pending_states_.begin(), pending_states_.end());
    }

    foreach (CVExecutionState* state, pending_states_) {
      if (!state_set.count(state)) {
        CVDEBUG("Removing duplicate state " << state << ":" << state->id());
        // Remove/delete states that are duplicates 
        cv_->executor()->remove_state_internal(state);
        ++stats::merged_states;
      } else {
        CVDEBUG("New stage from unique state " << state << ":" << state->id());
        // Create new stage and add to pending list
        pending_stages_.push_back(get_new_stage(state));
        ++stats::active_states;
      }
    }

    assert(!pending_stages_.empty()); 

    // Compute and output statistics for the previous round
    cv_->next_round();

    // Add all pending stages to active stage list
    stages_.insert(stages_.end(), 
                   pending_stages_.begin(), pending_stages_.end());

    pending_stages_.clear();
    pending_states_.clear();
  } 

  assert(!stages_.empty());
  assert(!stages_.back()->empty());

  return *(static_cast<klee::ExecutionState*>(stages_.back()->next_state()));
}

bool TrainingSearcher::empty() {
  reverse_foreach (SearcherStage* stage, stages_)
    if (!stage->empty()) return false;

  reverse_foreach (SearcherStage* stage, pending_stages_)
    if (!stage->empty()) return false;

  if (!pending_states_.empty())
    return false;

  return true;
}

SearcherStage* TrainingSearcher::get_new_stage(CVExecutionState* state) {
  return SearcherStageFactory::create(merger_, state);
}

void TrainingSearcher::add_state(CVExecutionState* state) {
  if (stages_.empty()) {
    stages_.push_back(get_new_stage(state));
  } else {
    if (!check_pending(state))
      stages_.back()->add_state(state);
  }
}

void TrainingSearcher::remove_state(CVExecutionState* state) {
  assert(!stages_.empty());
  stages_.back()->remove_state(state);
}

bool TrainingSearcher::check_pending(CVExecutionState* state) {
  if (pending_states_.count(state)) {
    CVDEBUG("Removing pending state " << state << ":" << state->id());
    // Remove State from current stage
    this->remove_state(state);

    if (ClientModelFlag == XPilot) {
      if (state->network_manager()->socket()->round() <= cv_->round()) {
        CVDEBUG("Removing state at merge event, wront round "
                << *(state->network_manager()->socket()) 
                << ", State" << *state);
        // Remove invalid state with unfinished network processing
        cv_->executor()->remove_state_internal(state);
        pending_states_.erase(state);
      } else {
        pending_states_.insert(state);
      }
    }

    return true;
  }
  return false;
}

void TrainingSearcher::notify(ExecutionEvent ev) {
  if (!stages_.empty())
    stages_.back()->notify(ev);

  switch(ev.event_type) {
    case CV_MERGE: {
      CVDEBUG("Merge event " << *ev.state);
      pending_states_.insert(ev.state);
      break;
    }
    case CV_FINISH: {
      CVDEBUG("Finish event " << *ev.state);
      if (!ev.state->network_manager()->socket()->is_open()) {
        CVMESSAGE("Finished state: " << *ev.state);
        cv_->executor()->add_finished_state(ev.state);
      }
      break;
    }
    case CV_SOCKET_WRITE:
    case CV_SOCKET_READ: {
      if (ClientModelFlag != XPilot)
        pending_states_.insert(ev.state);
      break;
    }
    case CV_SOCKET_SHUTDOWN: {
      cv_->executor()->setHaltExecution(true);
      break;
    }
    default:
      break;
  }
}

////////////////////////////////////////////////////////////////////////////////

} // end namespace cliver
