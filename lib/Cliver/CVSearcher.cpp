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
#include "klee/Internal/Module/InstructionInfoTable.h"

namespace cliver {

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
	if (et == CliverEvent::Network) {
		p->socket_log_index = state->network_manager()->socket_log_index();
	}
}

////////////////////////////////////////////////////////////////////////////////

TrainingSearcher::TrainingSearcher(klee::Searcher* base_searcher, 
		StateMerger* merger) 
	: CVSearcher(base_searcher, merger) {}

klee::ExecutionState &TrainingSearcher::selectState() {

	if (!phases_[TrainingProperty::NetworkClone].empty()) {
		CVExecutionState* state 
			= *(phases_[TrainingProperty::NetworkClone].begin());
		//cv_debug("State %d is in Training Phase NetworkClone at instruction id %d",
		//			state->id(), state->pc->info->id);
		return *(static_cast<klee::ExecutionState*>(state));
	}

	if (!phases_[TrainingProperty::Execute].empty()) {
		CVExecutionState* state 
			= *(phases_[TrainingProperty::Execute].begin());
		//cv_debug("State %d is in Training Phase Execute at instruction id %d",
		//			state->id(), state->pc->info->id);
		return *(static_cast<klee::ExecutionState*>(state));
	}

	if (!phases_[TrainingProperty::PrepareExecute].empty()) {
	  CVExecutionState* state = NULL;
		ExecutionStateSet modified_states;
	  foreach (state, phases_[TrainingProperty::PrepareExecute]) {
			TrainingProperty *p = static_cast<TrainingProperty*>(state->property());
			p->training_state = TrainingProperty::Execute;

			// Debug Output
			cv_debug("State %d is Preparing Execution for path (%d -> ...) in round %d",
					state->id(), state->pc->info->id, p->training_round);
			std::string pc_str;
			state->get_pc_string(pc_str);
			*cv_debug_stream << pc_str << "\n";

			p->start_instruction_id = state->pc->info->id;
			p->start_instruction = state->pc->inst;

			modified_states.insert(state);
		}

		foreach (state, modified_states) {
			phases_[TrainingProperty::PrepareExecute].erase(state);
		}

		ExecutionStateSet result;
		merger_->merge(modified_states, result);
		modified_states.swap(result);

		foreach (state, modified_states) {
			phases_[TrainingProperty::Execute].insert(state);
		}

		return selectState();
	}

	cv_error("no states remaining");

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
		TrainingProperty *p = static_cast<TrainingProperty*>(state->property());
		ExecutionStateSet &state_set = phases_[p->training_state];

		if (state_set.count(state) == 0) {
			unsigned i;
			for (i=0; i < TrainingProperty::EndState; i++) {
				if (phases_[i].count(state) != 0) {
					phases_[i].erase(state);
					break;
				}
			}
			if (i == TrainingProperty::EndState) {
				cv_error("state erase failed");
			}
		} else {
			state_set.erase(state);
		}
	}

	foreach (klee::ExecutionState* klee_state, added_states) {
		CVExecutionState *state = static_cast<CVExecutionState*>(klee_state);
		TrainingProperty *p = static_cast<TrainingProperty*>(state->property());
		ExecutionStateSet &state_set = phases_[p->training_state];
		state_set.insert(state);
	}
}

void TrainingSearcher::clone_for_network_events(CVExecutionState *state,
		CVExecutor* executor) {

	TrainingProperty *p = static_cast<TrainingProperty*>(state->property());
	p->training_state = TrainingProperty::NetworkClone;
	state->network_manager()->clear_sockets();

	// Debug Output
	cv_debug("State %d is Cloning at instruction id %d",
			state->id(), state->pc->info->id);
	std::string pc_str;
	state->get_pc_string(pc_str);
	*cv_debug_stream << pc_str << "\n";

	//bool is_open = state->network_manager()->sockets().back().is_open();
	bool is_open = true;

	SocketEventList* sel = NULL;
	unsigned count=0;
	
	// Clone a state for every message.
	foreach(sel, g_client_verifier->socket_events()) {
		const SocketEvent* se = NULL;
		foreach(se, *sel) {
			CVExecutionState* cloned_state = state->clone();
			cloned_state->network_manager()->clear_sockets();
			cloned_state->network_manager()->add_socket(*se, is_open);
			executor->add_state(cloned_state);
			count++;
		}
	}
	cv_debug("State %d is cloned into %d states at instruction id %d",
			state->id(),count,state->pc->info->id);
}

void TrainingSearcher::record_path(CVExecutionState *state,
		CVExecutor* executor) {
	TrainingProperty *p = static_cast<TrainingProperty*>(state->property());
	p->end_instruction_id = state->pc->info->id;
	p->end_instruction = state->pc->inst;

	// Debug Output
	cv_debug("State %d is Recording Path (%d -> %d) in round %d",
			state->id(), p->start_instruction_id, p->end_instruction_id,
			p->training_round);
	std::string start_str;
	std::string end_str;
	state->get_pc_string(start_str, p->start_instruction);
	state->get_pc_string(end_str, p->end_instruction);
	*cv_debug_stream << start_str << "\n";
	*cv_debug_stream << end_str << "\n";

	// Write to file (pathstart, pathend, path, message)...
	
	// update for next path
	p->training_state = TrainingProperty::PrepareExecute;
	p->training_round++;

}

void TrainingSearcher::handle_post_event(CVExecutionState *state,
		CVExecutor *executor, CliverEvent::Type et) {

	TrainingProperty* p = static_cast<TrainingProperty*>(state->property());
	TrainingSearcher* searcher 
		= static_cast<TrainingSearcher*>(g_client_verifier->searcher());

	switch(p->training_state) {

		case TrainingProperty::PrepareExecute:
			break;

		case TrainingProperty::Execute:
			if (et == CliverEvent::Training) {
				searcher->record_path(state, executor);
			}
			break;

		case TrainingProperty::NetworkClone:
			if (et == CliverEvent::Network) {
				searcher->record_path(state, executor);
			}
			break;
	}
}

void TrainingSearcher::handle_pre_event(CVExecutionState *state,
		CVExecutor* executor, CliverEvent::Type et) {

	TrainingProperty* p = static_cast<TrainingProperty*>(state->property());
	TrainingSearcher* searcher 
		= static_cast<TrainingSearcher*>(g_client_verifier->searcher());

	switch(p->training_state) {

		case TrainingProperty::PrepareExecute:
			break;

		case TrainingProperty::Execute:
			if (et == CliverEvent::Network) {
				searcher->clone_for_network_events(state,executor);
			}
			break;

		case TrainingProperty::NetworkClone:
			break;

	}
}

////////////////////////////////////////////////////////////////////////////////

} // end namespace cliver
