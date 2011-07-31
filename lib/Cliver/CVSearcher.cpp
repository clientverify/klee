//===-- CVSearcher.cpp ------------------------------------------*- C++ -*-===//
//
// <insert license>
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//

#include "CVSearcher.h"
#include "CVStream.h"

#include <boost/foreach.hpp>
#define foreach BOOST_FOREACH 

namespace cliver {

CVSearcher::CVSearcher(klee::Searcher* base_searcher) 
	: base_searcher_(base_searcher) {
}

bool CVSearcher::empty() {
	return state_count() == 0;
}

int CVSearcher::state_count() {
	int count = 0;
	ExecutionStateMap::iterator it, ie;
	for (it=states_.begin(), ie=states_.end(); it!=ie; ++it) {
		ExecutionStateSet &state_set = it->second;
		count += state_set.size();
	}
	return count;
}

klee::ExecutionState &CVSearcher::selectState() {

	klee::ExecutionState *klee_state = NULL;
	foreach (ExecutionStateMap::value_type v, states_) {
		ExecutionStateSet &state_set = v.second;

		if (state_set.size() > 0) {
			CVExecutionState *state = *(state_set.begin());
			klee_state = static_cast<klee::ExecutionState*>(state);
			return *klee_state;
		}
	}
	cv_error("no states remaining");
	return *klee_state;
}

void CVSearcher::update(klee::ExecutionState *current,
						const std::set<klee::ExecutionState*> &addedStates,
						const std::set<klee::ExecutionState*> &removedStates) {

	std::set<klee::ExecutionState*> removed_states(removedStates);
	if (current && removedStates.count(current) == 0) removed_states.insert(current);

	std::set<klee::ExecutionState*> added_states(addedStates);
	if (current && removedStates.count(current) == 0) added_states.insert(current);

	foreach (klee::ExecutionState* klee_state, removed_states) {
		CVExecutionState *state = static_cast<CVExecutionState*>(klee_state);
		ExecutionStateMap::iterator it, ie;
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

	foreach (klee::ExecutionState* klee_state, added_states) {
		CVExecutionState *state = static_cast<CVExecutionState*>(klee_state);
		states_[*state->info()].insert(state);
	}

	//base_searcher_->update(current, addedStates, removedStates);
	//klee::DFSSearcher *dfs_searcher = static_cast<klee::DFSSearcher*>(base_searcher_);
	//if (state_count() != dfs_searcher->states.size()) {
	//	cv_error("cvsearcher: %d, dfs_searcher: %d",
	//			state_count(), dfs_searcher->states.size());
	//}
}

} // end namespace cliver
