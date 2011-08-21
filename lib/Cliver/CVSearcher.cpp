//===-- CVSearcher.cpp ------------------------------------------*- C++ -*-===//
//
// <insert license>
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//

#include "CVSearcher.h"
#include "StateMerger.h"
#include "ClientVerifier.h"
#include "NetworkManager.h"
#include "PathManager.h"
#include "klee/Internal/Module/InstructionInfoTable.h"

#include <boost/archive/text_oarchive.hpp>
#include <boost/archive/text_iarchive.hpp>

namespace cliver {

////////////////////////////////////////////////////////////////////////////////

llvm::cl::opt<bool>
DebugSearcher("debug-searcher",llvm::cl::init(false));

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

CVSearcher::CVSearcher(klee::Searcher* base_searcher, StateMerger* merger) 
	: base_searcher_(base_searcher), merger_(merger), current_property_(0) {
}

bool CVSearcher::empty() {
	return state_count() == 0;
}

int CVSearcher::state_count() {
	int count = 0;
	ExecutionStatePropertyMap::iterator it, ie;
	for (it=states_.begin(), ie=states_.end(); it!=ie; ++it) {
		ExecutionStateSet &state_set = it->second;
		count += state_set.size();
	}
	return count;
}

klee::ExecutionState &CVSearcher::selectState() {

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
					g_client_verifier->print_current_statistics();

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

void CVSearcher::update(klee::ExecutionState *current,
						const std::set<klee::ExecutionState*> &addedStates,
						const std::set<klee::ExecutionState*> &removedStates) {

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
			cv_error("CVSearcher could not erase state!");
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

////////////////////////////////////////////////////////////////////////////////

LogIndexSearcher::LogIndexSearcher(klee::Searcher* base_searcher, 
		StateMerger* merger) 
	: CVSearcher(base_searcher, merger) {}

void LogIndexSearcher::handle_pre_event(CVExecutionState *state,
		CVExecutor* executor, CliverEvent::Type et) {}

void LogIndexSearcher::handle_post_event(CVExecutionState *state,
		CVExecutor* executor, CliverEvent::Type et) {

	LogIndexProperty* p = static_cast<LogIndexProperty*>(state->property());
	if (et == CliverEvent::NetworkSend || et == CliverEvent::NetworkRecv) {
		p->socket_log_index = state->network_manager()->socket_log_index();
	}
}

////////////////////////////////////////////////////////////////////////////////

OutOfOrderTrainingSearcher::OutOfOrderTrainingSearcher(klee::Searcher* base_searcher, 
		StateMerger* merger) 
	: CVSearcher(base_searcher, merger), paths_(new PathSet()) {}

klee::ExecutionState &OutOfOrderTrainingSearcher::selectState() {

	if (!phases_[PathProperty::NetworkClone].empty()) {
		CVExecutionState* state 
			= *(phases_[PathProperty::NetworkClone].begin());
		return *(static_cast<klee::ExecutionState*>(state));
	}

	if (!phases_[PathProperty::Execute].empty()) {
		CVExecutionState* state 
			= *(phases_[PathProperty::Execute].begin());
		return *(static_cast<klee::ExecutionState*>(state));
	}

	if (!phases_[PathProperty::PrepareExecute].empty()) {
		CVDEBUG("Current Paths (" << paths_->size() << ")");
		foreach(PathManager* path, *paths_) {
			CVDEBUG(*path);
		}
		CVDEBUG("Current States (" 
				<< phases_[PathProperty::PrepareExecute].size() << ")");

		ExecutionStateSet to_merge(phases_[PathProperty::PrepareExecute]);

		ExecutionStateSet result;
		if (!to_merge.empty())
			merger_->merge(to_merge, result);
		
		CVDEBUG("Current states (after mergin'): " << result.size());

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

		return selectState();
	}

	cv_error("no states remaining");

	// XXX better error handling
	// This will never execute after cv_error
	klee::ExecutionState *null_state = NULL;
	return *null_state;
}

void OutOfOrderTrainingSearcher::update(klee::ExecutionState *current,
						const std::set<klee::ExecutionState*> &addedStates,
						const std::set<klee::ExecutionState*> &removedStates) {

	std::set<klee::ExecutionState*> removed_states(removedStates);
	std::set<klee::ExecutionState*> added_states(addedStates);

	if (current && removedStates.count(current) == 0) {
		removed_states.insert(current);
		added_states.insert(current);
	}

	foreach (klee::ExecutionState* klee_state, removed_states) {
		CVExecutionState *state = static_cast<CVExecutionState*>(klee_state);
		PathProperty *p = static_cast<PathProperty*>(state->property());
		ExecutionStateSet &state_set = phases_[p->phase];

		if (state_set.count(state) == 0) {
			unsigned i;
			for (i=0; i < PathProperty::EndState; i++) {
				if (phases_[i].count(state) != 0) {
					phases_[i].erase(state);
					break;
				}
			}
			if (i == PathProperty::EndState) {
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

void OutOfOrderTrainingSearcher::clone_for_network_events(CVExecutionState *state,
		CVExecutor* executor, CliverEvent::Type et) {

	PathProperty *p = static_cast<PathProperty*>(state->property());
	p->phase = PathProperty::NetworkClone;
	state->network_manager()->clear_sockets();

	bool is_open = true;

	unsigned count=0;
	
	// Clone a state for every message.
	SocketEventList* sel = NULL;
	foreach(sel, g_client_verifier->socket_events()) {
		const SocketEvent* se = NULL;
		foreach(se, *sel) {
			if ((CliverEvent::NetworkSend == et && SocketEvent::SEND == se->type)
				|| (CliverEvent::NetworkRecv == et && SocketEvent::RECV == se->type)) {
				CVExecutionState* cloned_state = state->clone();
				cloned_state->network_manager()->clear_sockets();
				cloned_state->network_manager()->add_socket(*se, is_open);
				cloned_state->path_manager()->add_message(se);
				executor->add_state(cloned_state);
				count++;
			}
		}
	}

	CVDEBUG_S(state->id(), "Cloning into " << count << " states in property "
			<< *p << " at " << *state->pc);
}

void OutOfOrderTrainingSearcher::record_path(CVExecutionState *state,
		CVExecutor* executor, CliverEvent::Type et) {

	PathProperty *p = static_cast<PathProperty*>(state->property());
	p->path_range = PathRange(p->path_range.start(), state->prevPC);
	state->path_manager()->set_range(p->path_range);

	CVDEBUG_S(state->id(), "Recording path (length "
			<< state->path_manager()->length() << ") "<< *p 
			<< " [Start " << *p->path_range.kinsts().first << "]"
			<< " [End "   << *p->path_range.kinsts().second << "]");

	// Write to file (pathstart, pathend, path, message)...
	
	if (paths_->contains(state->path_manager())) {

		if (PathManager* mpath = paths_->merge(state->path_manager())) {
			CVDEBUG_S(state->id(), "Adding new message, mcount is now "
					<< mpath->messages().size());
		} else {
			CVDEBUG_S(state->id(), "Path already contains message");
		}
	} else {
		if (paths_->add(state->path_manager()->clone())) {
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

void OutOfOrderTrainingSearcher::handle_post_event(CVExecutionState *state,
		CVExecutor *executor, CliverEvent::Type et) {

	PathProperty* p = static_cast<PathProperty*>(state->property());
	OutOfOrderTrainingSearcher* searcher 
		= static_cast<OutOfOrderTrainingSearcher*>(g_client_verifier->searcher());

	switch(p->phase) {

		case PathProperty::PrepareExecute:
			break;

		case PathProperty::Execute:
			break;

		case PathProperty::NetworkClone:
			if (et == CliverEvent::NetworkSend || et == CliverEvent::NetworkRecv) {
				searcher->record_path(state, executor, et);
			}
			break;
	}
}

void OutOfOrderTrainingSearcher::handle_pre_event(CVExecutionState *state,
		CVExecutor* executor, CliverEvent::Type et) {

	PathProperty* p = static_cast<PathProperty*>(state->property());
	OutOfOrderTrainingSearcher* searcher 
		= static_cast<OutOfOrderTrainingSearcher*>(g_client_verifier->searcher());

	switch(p->phase) {

		case PathProperty::PrepareExecute:
			break;

		case PathProperty::Execute:
			if (et == CliverEvent::NetworkSend || et == CliverEvent::NetworkRecv) {
				searcher->clone_for_network_events(state,executor, et);
			}
			else if (et == CliverEvent::Training) {
				searcher->record_path(state, executor, et);
			}
			break;

		case PathProperty::NetworkClone:
			break;

	}
}

////////////////////////////////////////////////////////////////////////////////

TrainingSearcher::TrainingSearcher(klee::Searcher* base_searcher, 
		StateMerger* merger) 
	: CVSearcher(base_searcher, merger), paths_(new PathSet()) {}

klee::ExecutionState &TrainingSearcher::selectState() {

	if (!phases_[PathProperty::Execute].empty()) {
		CVExecutionState* state = *(phases_[PathProperty::Execute].begin());
		return *(static_cast<klee::ExecutionState*>(state));
	}

	if (!phases_[PathProperty::PrepareExecute].empty()) {
		// Print stats
		g_client_verifier->print_current_statistics();
		CVMESSAGE("Current Paths (" << paths_->size() << ")");
		foreach(PathManager* path, *paths_) {
			CVDEBUG(*path);
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

	std::set<klee::ExecutionState*> removed_states(removedStates);
	std::set<klee::ExecutionState*> added_states(addedStates);

	if (current && removedStates.count(current) == 0) {
		removed_states.insert(current);
		added_states.insert(current);
	}

	foreach (klee::ExecutionState* klee_state, removed_states) {
		CVExecutionState *state = static_cast<CVExecutionState*>(klee_state);
		PathProperty *p = static_cast<PathProperty*>(state->property());
		ExecutionStateSet &state_set = phases_[p->phase];

		if (state_set.count(state) == 0) {
			unsigned i;
			for (i=0; i < PathProperty::EndState; i++) {
				if (phases_[i].count(state) != 0) {
					phases_[i].erase(state);
					break;
				}
			}
			if (i == PathProperty::EndState) {
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

void TrainingSearcher::record_path(CVExecutionState *state,
		CVExecutor* executor, CliverEvent::Type et) {

	PathProperty *p = static_cast<PathProperty*>(state->property());
	p->path_range = PathRange(p->path_range.start(), state->prevPC);
	state->path_manager()->set_range(p->path_range);
	if (et == CliverEvent::NetworkSend ||
			et == CliverEvent::NetworkRecv) {
		if (Socket* s = state->network_manager()->socket()) {
			const SocketEvent* se = &s->previous_event();
			state->path_manager()->add_message(se);
		} else {
			cv_error("No message in state");
		}
	} else {
		cv_error("invalid cliver event for recording path");
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
	std::ostream *file = g_client_verifier->openOutputFile(filename.str());
	state->path_manager()->write(*file);
	delete file;
		
	if (paths_->contains(state->path_manager())) {
		if (PathManager* mpath = paths_->merge(state->path_manager())) {
			CVDEBUG_S(state->id(), "Adding new message, mcount is now "
					<< mpath->messages().size());
		} else {
			CVDEBUG_S(state->id(), "Path already contains message");
		}
	} else {
		if (paths_->add(state->path_manager()->clone())) {
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


void TrainingSearcher::handle_post_event(CVExecutionState *state,
		CVExecutor *executor, CliverEvent::Type et) {

	PathProperty* p = static_cast<PathProperty*>(state->property());
	TrainingSearcher* searcher 
		= static_cast<TrainingSearcher*>(g_client_verifier->searcher());

	switch(et) {
		case CliverEvent::NetworkSend:
		case CliverEvent::NetworkRecv:
			searcher->record_path(state, executor, et);
			break;
		default:
			break;
	}
}

void TrainingSearcher::handle_pre_event(CVExecutionState *state,
		CVExecutor* executor, CliverEvent::Type et) {}

////////////////////////////////////////////////////////////////////////////////

} // end namespace cliver
