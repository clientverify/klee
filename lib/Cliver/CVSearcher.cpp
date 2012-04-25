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
      CVExecutionStateDeleter cv_deleter;
      stages_.back()->clear(&cv_deleter);
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

    // XXX Hack to prune state constraints
    ExecutionStateSet state_set, merged_set;
    state_set.insert(state);
    merger_->merge(state_set, merged_set);

    // Create new stage and add to pending list
    pending_stages_.push_back(get_new_stage(*(merged_set.begin())));

    return true;
  }
  return false;
}

void VerifySearcher::notify(ExecutionEvent ev) {
  stages_.back()->notify(ev);

  switch(ev.event_type) {
    case CV_SOCKET_WRITE:
    case CV_SOCKET_READ: {
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

MergeVerifySearcher::MergeVerifySearcher(ClientVerifier* cv, StateMerger* merger)
  : VerifySearcher(cv, merger) {}

bool MergeVerifySearcher::check_pending(CVExecutionState* state) {
  if (pending_states_.count(state)) {
    // Should only be 1 for now.
    assert(pending_states_.size() == 1);

    // Remove from set
    pending_states_.erase(state);

    // Remove State from current stage
    this->remove_state(state);

    if (state->network_manager()->socket()->round() > cv_->round()) {

      // XXX Hack to prune state constraints
      //ExecutionStateSet state_set, merged_set;
      //state_set.insert(state);
      //merger_->merge(state_set, merged_set);

      // Create new stage and add to pending list
      pending_stages_.push_back(get_new_stage(state));
    } else {
      CVDEBUG("Removing state at merge event, wrong round: SocketRN:" <<
              state->network_manager()->socket()->round() <<
             " <= StateRN:" << cv_->round());
      // Remove invalid state with unfinished network processing
      cv_->executor()->remove_state_internal(state);

    }
    return true;
  }
  return false;
}

void MergeVerifySearcher::notify(ExecutionEvent ev) {
  stages_.back()->notify(ev);

  switch(ev.event_type) {
    case CV_MERGE: {
      CVDEBUG("MERGE EVENT! " << *ev.state);
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

TrainingSearcher::TrainingSearcher(ClientVerifier *cv, 
                                         StateMerger* merger)
  : CVSearcher(NULL, cv, merger) {}

klee::ExecutionState &TrainingSearcher::selectState() {
  //klee::TimerStatIncrementer timer(stats::searcher_time);
 
  while (!stages_.empty() && stages_.back()->empty()) {
    delete stages_.back();
    stages_.pop_back();
  }
 
  if (stages_.empty()) {
    assert(!pending_states_.empty());

    // Prune state constraints and merge states
    ExecutionStateSet state_set, merged_set;
    state_set.insert(pending_states_.begin(), pending_states_.end());
    merger_->merge(state_set, merged_set);

    foreach (CVExecutionState* state, pending_states_) {
      if (!merged_set.count(state)) {
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

void TrainingSearcher::update(klee::ExecutionState *current,
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
    CVDEBUG("New stage from state " << state << ":" << state->id());
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

    return true;
  }
  return false;
}

void TrainingSearcher::notify(ExecutionEvent ev) {
  stages_.back()->notify(ev);

  switch(ev.event_type) {
    case CV_SOCKET_WRITE:
    case CV_SOCKET_READ: {
      CVDEBUG("Inserting pending state " << ev.state << ":" << ev.state->id());
			pending_states_.insert(ev.state);
      break;
    }
    default:
      break;
  }
}

////////////////////////////////////////////////////////////////////////////////

} // end namespace cliver
