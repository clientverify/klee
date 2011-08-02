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
#include "ConstraintPruner.h"

#include <boost/foreach.hpp>
#define foreach BOOST_FOREACH 

namespace cliver {

struct MergeInfo {
	AddressSpaceGraph *graph;
};

StateMerger::StateMerger(ConstraintPruner *pruner) : pruner_(pruner) {}

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

bool StateMerger::compare_constraints(klee::ConstraintManager &a, 
		klee::ConstraintManager &b) {

	std::set< klee::ref<klee::Expr> > set_a(a.begin(), a.end());
	std::set< klee::ref<klee::Expr> > set_b(b.begin(), b.end());
	std::set< klee::ref<klee::Expr> > common;

	std::set_intersection(set_a.begin(), set_a.end(), set_b.begin(), set_b.end(),
			std::inserter(common, common.begin()));

	if (common.size() == set_a.size() &&
			common.size() == set_b.size()) {
		return true;
	} else {
		CVDEBUG("constraints do not match");
		//*cv_debug_stream << "(1)------------------------------------------\n";
		//foreach( klee::ref<klee::Expr> e, set_a) {
		//	*cv_debug_stream << e << "\n";
		//}
		//*cv_debug_stream << "(2)------------------------------------------\n";
		//foreach( klee::ref<klee::Expr> e, set_b) {
		//	*cv_debug_stream << e << "\n";
		//}
		return false;
	}
}

void StateMerger::merge(ExecutionStateSet &state_set, 
		ExecutionStateSet &merged_set) {

	std::map<CVExecutionState*, MergeInfo> merge_info;
	foreach (CVExecutionState* state, state_set) {
		merge_info[state] = MergeInfo();
	}

	foreach (CVExecutionState* state, state_set) {
		AddressSpaceGraph *graph = new AddressSpaceGraph(state);
		graph->build();
		pruner_->prune_constraints(*state, *graph);
		merge_info[state].graph = graph;
	}

	std::vector<CVExecutionState*> worklist(state_set.begin(), state_set.end());
	std::vector<CVExecutionState*> unique_states;

  do {
		CVExecutionState* state = worklist.back();
		worklist.pop_back();
		std::vector<CVExecutionState*>::iterator it=worklist.begin(), ie=worklist.end();
		for (; it!=ie; ++it) {
			if (merge_info[state].graph->equals(*merge_info[*it].graph)) {
				if (compare_constraints(state->constraints, (*it)->constraints)) {
					break;
				}
			}
		}
		if (it == ie) {
			unique_states.push_back(state);
			merged_set.insert(state);
		}

  } while (!worklist.empty());


	CVDEBUG("Found " << state_set.size() - unique_states.size() 
			<< " duplicates out of " << state_set.size() 
			<< ", now " << unique_states.size() << " states remain.");

	//merged_set.insert(state_set.begin(), state_set.end());
}

} // end namespace cliver
