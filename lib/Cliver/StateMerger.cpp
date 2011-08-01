//===-- StateMerger.cpp -----------------------------------------*- C++ -*-===//
//
// <insert license>
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//

#include "CVSearcher.h"
#include "CVStream.h"
#include "StateMerger.h"
#include "AddressSpaceGraph.h"

#include <boost/foreach.hpp>
#define foreach BOOST_FOREACH 

namespace cliver {

struct MergeInfo {
	AddressSpaceGraph *graph;
};

StateMerger::StateMerger() {}

// Pre-merging Steps
// 1. Build AddressSpaceGraph on each state
// 2. Prune symbolic variables that are not resident in graph (optional) 
//
// Merging Steps (abort if not equal at each step)
// 1. Compare instruction pointer (pc) 
// 2. Compare call stack (stackframe)
// 3. Compare AddressSpaceGraph structure
// 4. Canonicalize symbolic variables
// 5. Compare constraint sets

void StateMerger::merge(ExecutionStateSet &state_set, 
		ExecutionStateSet &merged_set) {

	std::map<CVExecutionState*, MergeInfo> merge_info;
	foreach (CVExecutionState* state, state_set) {
		merge_info[state] = MergeInfo();
	}

	foreach (CVExecutionState* state, state_set) {
		merge_info[state].graph = new AddressSpaceGraph(state);
		merge_info[state].graph->build();
	}

	merged_set.insert(state_set.begin(), state_set.end());
}

} // end namespace cliver
