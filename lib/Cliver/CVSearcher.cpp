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


void boost::throw_exception(std::exception const& e) {}

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

} // end namespace cliver
