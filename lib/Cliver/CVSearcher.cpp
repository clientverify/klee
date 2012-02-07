//===-- CVSearcher.cpp ------------------------------------------*- C++ -*-===//
//
// <insert license>
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//

#include "CVCommon.h"
#include "cliver/CVSearcher.h"
#include "cliver/CVExecutionState.h"
#include "cliver/CVExecutor.h"
#include "cliver/StateMerger.h"
#include "cliver/ClientVerifier.h"
#include "cliver/NetworkManager.h"
#include "cliver/PathManager.h"
#include "cliver/PathSelector.h"
#include "cliver/PathTree.h"

#include "klee/Internal/Module/InstructionInfoTable.h"

#include <boost/archive/text_oarchive.hpp>
#include <boost/archive/text_iarchive.hpp>

#include "llvm/Support/raw_ostream.h"

namespace cliver {

llvm::cl::opt<bool>
DebugSearcher("debug-searcher",llvm::cl::init(false));

llvm::cl::opt<bool>
PrintTrainingPaths("print-training-paths",llvm::cl::init(false));

llvm::cl::opt<int>
KLookAheadValue("klookahead",llvm::cl::init(16));

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

LogIndexSearcher::LogIndexSearcher(klee::Searcher* base_searcher, 
		ClientVerifier* cv, StateMerger* merger) 
	: CVSearcher(base_searcher, cv, merger) {}

bool LogIndexSearcher::empty() {
	return state_count() == 0;
}

int LogIndexSearcher::state_count() {
	int count = 0;
	ExecutionStatePropertyMap::iterator it, ie;
	for (it=states_.begin(), ie=states_.end(); it!=ie; ++it) {
		ExecutionStateSet &state_set = it->second;
		count += state_set.size();
	}
	return count;
}

klee::ExecutionState &LogIndexSearcher::selectState() {
	//klee::TimerStatIncrementer timer(stats::searcher_time);

	// Walk the ExecutionStatePropertyMap from the oldest to the newest,
	// or whatever ordering the ExecutionStateProperty induces
	// and select a state from the earliest grouping
	ExecutionStatePropertyMap::iterator it, ie;
	for (it=states_.begin(), ie=states_.end(); it!=ie; ++it) {
		ExecutionStateSet &state_set = it->second;

		if (!state_set.empty()) {

			// Erase all of the empty sets up to the current one
			if (states_.begin() != it) {
				ExecutionStatePropertyMap::iterator erase_it=states_.begin();
				while (erase_it != it) {
					ExecutionStateProperty* p = erase_it->first;
					states_.erase(erase_it++);
					delete p;
				}
				//states_.erase(states_.begin(), it);

				// Merge each group of states
				ExecutionStatePropertyMap::iterator merge_it = it;
				for (; merge_it!=ie; ++merge_it) {
					ExecutionStateSet &merge_state_set = merge_it->second;
					ExecutionStateSet result;
					merger_->merge(merge_state_set, result);

					// Update stats
					stats::merged_states += merge_state_set.size() - result.size();
					stats::active_states += result.size();

					// Print new stats
					cv_->print_current_statistics();

					// swap state sets
					merge_state_set.swap(result);
				}
			}

			// Select the first state in this set, cast the CVExecutionState
			// pointer into a klee::ExecutionState ptr, then dereference
			return *(static_cast<klee::ExecutionState*>(*(state_set.begin())));
		}
	}
	cv_error("no states remaining");

	// This will never execute after cv_error
	klee::ExecutionState *null_state = NULL;
	return *null_state;
}

void LogIndexSearcher::update(klee::ExecutionState *current,
						const std::set<klee::ExecutionState*> &addedStates,
						const std::set<klee::ExecutionState*> &removedStates) {
	//klee::TimerStatIncrementer timer(stats::searcher_time);

	std::set<klee::ExecutionState*> removed_states(removedStates);
	std::set<klee::ExecutionState*> added_states(addedStates);

	if (current && removedStates.count(current) == 0) {
		removed_states.insert(current);
		added_states.insert(current);
	}

	foreach (klee::ExecutionState* klee_state, removed_states) {
		CVExecutionState *state = static_cast<CVExecutionState*>(klee_state);
		// Iterate over all the ExecutionState groups to determine
		// if they contain state, we can't look up the state via it's
		// ExecutionStateProperty because it might have changed since insertion
		ExecutionStatePropertyMap::iterator it, ie;
		for (it=states_.begin(), ie=states_.end(); it!=ie; ++it) {
			ExecutionStateSet &state_set = it->second;
			if (state_set.count(state)) {
				state_set.erase(state);
				break;
			}
		}
		if (it == ie) 
			cv_error("LogIndexSearcher could not erase state!");
	}

	// Insert the added states into the associated group
	foreach (klee::ExecutionState* klee_state, added_states) {
		CVExecutionState *state = static_cast<CVExecutionState*>(klee_state);
		ExecutionStatePropertyMap::iterator it = states_.find(state->property());
		if (it != states_.end()) {
			(it->second).insert(state);
		} else {
			ExecutionStateProperty* p = state->property()->clone();
			states_[p].insert(state);
		}
	}
}

void LogIndexSearcher::notify(ExecutionEvent ev) {
  switch(ev.event_type) {
    case CV_SOCKET_WRITE:
    case CV_SOCKET_READ: {
      CVExecutionState *state = static_cast<CVExecutionState*>(ev.state);
      LogIndexProperty* p = static_cast<LogIndexProperty*>(state->property());
      p->socket_log_index = state->network_manager()->socket_log_index();
      break;
    }
    default:
      break;
  }
}

////////////////////////////////////////////////////////////////////////////////

TrainingSearcher::TrainingSearcher(klee::Searcher* base_searcher, 
		ClientVerifier* cv, StateMerger* merger) 
	: CVSearcher(base_searcher, cv, merger), paths_(new PathManagerSet()) {}

klee::ExecutionState &TrainingSearcher::selectState() {
	//klee::TimerStatIncrementer timer(stats::searcher_time);

	if (!phases_[PathProperty::Execute].empty()) {
		CVExecutionState* state = *(phases_[PathProperty::Execute].begin());
		return *(static_cast<klee::ExecutionState*>(state));
	}

	if (!phases_[PathProperty::PrepareExecute].empty()) {
		// Print stats
		cv_->print_current_statistics();
		CVMESSAGE("Current Paths (" << paths_->size() << ")");
		if (PrintTrainingPaths) {
			foreach(PathManager* path, *paths_) {
				CVDEBUG(*path);
			}
		}
		CVMESSAGE("Current States (" 
				<< phases_[PathProperty::PrepareExecute].size() << ")");

		ExecutionStateSet to_merge(phases_[PathProperty::PrepareExecute]);

		ExecutionStateSet result;
		if (!to_merge.empty())
			merger_->merge(to_merge, result);
		
		CVMESSAGE("Current states after mergin' (" << result.size() << ")");

		phases_[PathProperty::PrepareExecute].clear();

	  foreach (CVExecutionState* state, result) {
			PathProperty *p = static_cast<PathProperty*>(state->property());
			p->phase = PathProperty::Execute;
			p->path_range = PathRange(state->prevPC, NULL);
			state->reset_path_manager();

			CVDEBUG_S(state->id(), "Preparing Execution in " << *p 
					<< " in round " << p->round << " " << *state->prevPC);
		}

		foreach (CVExecutionState* state, result) {
			phases_[PathProperty::Execute].insert(state);
		}
	
		CVExecutionState* state = *(phases_[PathProperty::Execute].begin());                                                                                                                                                 
		Socket *socket = state->network_manager()->socket();                                                                                                                                                                 
		CVMESSAGE("SocketEvent: " << *socket);   

		return selectState();
	}

	cv_error("no states remaining");

	// XXX better error handling
	// This will never execute after cv_error
	klee::ExecutionState *null_state = NULL;
	return *null_state;
}

void TrainingSearcher::update(klee::ExecutionState *current,
		const std::set<klee::ExecutionState*> &addedStates,
		const std::set<klee::ExecutionState*> &removedStates) {
	//klee::TimerStatIncrementer timer(stats::searcher_time);

	const std::set<klee::ExecutionState*>& removed_states = removedStates;
	const std::set<klee::ExecutionState*>& added_states = addedStates;

	if (current && removed_states.count(current) == 0) {
		CVExecutionState *state = static_cast<CVExecutionState*>(current);
		PathProperty *p = static_cast<PathProperty*>(state->property());
		ExecutionStateSet &state_set = phases_[p->phase];
		if (state_set.count(state) == 0) {
			unsigned i;
			for (i=0; i < PathProperty::EndPhase; i++) {
				if (phases_[i].count(state) != 0) {
					phases_[i].erase(state);
					break;
				}
			}
			if (i == PathProperty::EndPhase) {
				cv_error("state erase failed");
			}
			state_set.insert(state);
		}
	}

	foreach (klee::ExecutionState* klee_state, removed_states) {
		CVExecutionState *state = static_cast<CVExecutionState*>(klee_state);
		PathProperty *p = static_cast<PathProperty*>(state->property());
		ExecutionStateSet &state_set = phases_[p->phase];

		if (state_set.count(state) == 0) {
			unsigned i;
			for (i=0; i < PathProperty::EndPhase; i++) {
				if (phases_[i].count(state) != 0) {
					phases_[i].erase(state);
					break;
				}
			}
			if (i == PathProperty::EndPhase) {
				cv_error("state erase failed");
			}
		} else {
			state_set.erase(state);
		}
	}

	foreach (klee::ExecutionState* klee_state, added_states) {
		CVExecutionState *state = static_cast<CVExecutionState*>(klee_state);
		PathProperty *p = static_cast<PathProperty*>(state->property());
		ExecutionStateSet &state_set = phases_[p->phase];
		state_set.insert(state);
	}
}

bool TrainingSearcher::empty() {
	if (phases_[PathProperty::Execute].empty() &&
		  phases_[PathProperty::PrepareExecute].empty()) {
		return true;
	}
	return false;
}

void TrainingSearcher::record_path(CVExecutionState *state) {
	//klee::TimerStatIncrementer timer(stats::searcher_time);

	PathProperty *p = static_cast<PathProperty*>(state->property());
	p->path_range = PathRange(p->path_range.start(), state->prevPC);
	state->path_manager()->set_range(p->path_range);
  if (Socket* s = state->network_manager()->socket()) {
    const SocketEvent* se = &s->previous_event();
    static_cast<TrainingPathManager*>(
        state->path_manager())->add_socket_event(se);
  } else {
    cv_error("No message in state");
  }
 
	CVDEBUG_S(state->id(), "Recording path (length "
			<< state->path_manager()->length() << ") "<< *p 
			<< " [Start " << *p->path_range.kinsts().first << "]"
			<< " [End "   << *p->path_range.kinsts().second << "]");

	// Write to file (pathstart, pathend, path, message)...
	std::stringstream filename;
	filename << "state_" << state->id() 
		<< "-round_" << p->round 
		<< "-length_" << state->path_manager()->length() 
		<< ".tpath";
	std::ostream *file = cv_->openOutputFile(filename.str());
	static_cast<TrainingPathManager*>(state->path_manager())->write(*file);
	delete file;
		
	if (paths_->contains(state->path_manager())) {
		if (PathManager* mpath = paths_->merge(state->path_manager())) {
			CVDEBUG_S(state->id(), "Adding new message, mcount is now "
					<< static_cast<TrainingPathManager*>(mpath)->socket_events().size());
		} else {
			CVDEBUG_S(state->id(), "Path already contains message");
		}
	} else {
		if (paths_->insert(state->path_manager()->clone())) {
			CVDEBUG_S(state->id(), "Adding new path, pcount is now " 
					<< paths_->size());
		} else {
			cv_error("error adding new path");
		}
	}
	
	// update for next path
	p->phase = PathProperty::PrepareExecute;
	p->round++;
}

void TrainingSearcher::notify(ExecutionEvent ev) {
  switch(ev.event_type) {
    case CV_SOCKET_WRITE:
    case CV_SOCKET_READ: {
			record_path(static_cast<CVExecutionState*>(ev.state));
      break;
    }
    default:
      break;
  }
}

////////////////////////////////////////////////////////////////////////////////

VerifySearcher::VerifySearcher(ClientVerifier* cv, StateMerger* merger)
  : CVSearcher(NULL, cv, merger) {}

klee::ExecutionState &VerifySearcher::selectState() {
  //klee::TimerStatIncrementer timer(stats::searcher_time);
  
  if (!pending_stages_.empty()) {
    // Compute and output statistics for the previous round
    cv_->print_current_statistics();

    // Add pending stage to active stage list
    stages_.push_back(pending_stages_.back());
    pending_stages_.pop_back();
  }

  while (!stages_.empty() && stages_.back()->empty()) {
    delete stages_.back();
    stages_.pop_back();
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
  reverse_foreach (SearcherStage* stage, stages_)
    if (!stage->empty()) return false;

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

NewTrainingSearcher::NewTrainingSearcher(ClientVerifier *cv, 
                                         StateMerger* merger)
  : CVSearcher(NULL, cv, merger) {}

klee::ExecutionState &NewTrainingSearcher::selectState() {
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
    cv_->print_current_statistics();

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

void NewTrainingSearcher::update(klee::ExecutionState *current,
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

bool NewTrainingSearcher::empty() {
  reverse_foreach (SearcherStage* stage, stages_)
    if (!stage->empty()) return false;

  reverse_foreach (SearcherStage* stage, pending_stages_)
    if (!stage->empty()) return false;

  if (!pending_states_.empty())
    return false;

  return true;
}

SearcherStage* NewTrainingSearcher::get_new_stage(CVExecutionState* state) {
  return SearcherStageFactory::create(merger_, state);
}

void NewTrainingSearcher::add_state(CVExecutionState* state) {
  if (stages_.empty()) {
    CVDEBUG("New stage from state " << state << ":" << state->id());
    stages_.push_back(get_new_stage(state));
  } else {
    if (!check_pending(state))
      stages_.back()->add_state(state);
  }
}

void NewTrainingSearcher::remove_state(CVExecutionState* state) {
  assert(!stages_.empty());
  stages_.back()->remove_state(state);
}

bool NewTrainingSearcher::check_pending(CVExecutionState* state) {
  if (pending_states_.count(state)) {
    CVDEBUG("Removing pending state " << state << ":" << state->id());

    // Remove State from current stage
    this->remove_state(state);

    return true;
  }
  return false;
}

void NewTrainingSearcher::notify(ExecutionEvent ev) {
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
