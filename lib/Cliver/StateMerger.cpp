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

void StateMerger::merge(ExecutionStateSet &state_set, 
		ExecutionStateSet &merged_set) {

	std::map<CVExecutionState*, MergeInfo> merge_info;
	foreach (CVExecutionState* state, state_set) {
		merge_info[state] = MergeInfo();
	}

	foreach (CVExecutionState* state, state_set) {
		AddressSpaceGraph *graph = new AddressSpaceGraph(state);
		graph->build();
		pruner_->prune(*state, *graph);
		merge_info[state].graph = graph;
	}

	std::vector<CVExecutionState*> worklist(state_set.begin(), state_set.end());
	std::vector<CVExecutionState*> unique_states;

  do {
		CVExecutionState* merge_state = worklist.back();
		worklist.pop_back();
		std::vector<CVExecutionState*>::iterator it=worklist.begin(), ie=worklist.end();
		for (; it!=ie; ++it) {
			if (merge_info[merge_state].graph->equals(*merge_info[*it].graph)) {
				std::set< klee::ref<klee::Expr> > 
					merge_constraints(merge_state->constraints.begin(), merge_state->constraints.end());
				std::set< klee::ref<klee::Expr> > 
					constraints((*it)->constraints.begin(), (*it)->constraints.end());
			
				std::set< klee::ref<klee::Expr> > common_constraints;
				std::set_intersection(
						merge_constraints.begin(), merge_constraints.end(),
						constraints.begin(), constraints.end(),
						std::inserter(common_constraints, common_constraints.begin()));
				if (common_constraints.size() == merge_constraints.size() &&
						common_constraints.size() == constraints.size()) {
					break;
				} else {
					CVDEBUG("constraints do not match");
					*cv_debug_stream << "(1)----------------------------------------" << "\n";
					foreach( klee::ref<klee::Expr> e, merge_constraints) {
						*cv_debug_stream << e << "\n";
					}
					*cv_debug_stream << "(2)----------------------------------------" << "\n";
					foreach( klee::ref<klee::Expr> e, constraints) {
						*cv_debug_stream << e << "\n";
					}
					*cv_debug_stream << "----------------------------------------" << "\n";
				}
			}
		}
		if (it == ie) {
			unique_states.push_back(merge_state);
			merged_set.insert(merge_state);
		}

  } while (!worklist.empty());


	CVDEBUG("Found " << state_set.size() - unique_states.size() << " duplicates out of "
			<< state_set.size() << ", now " << unique_states.size() << " states remain.");


	//merged_set.insert(state_set.begin(), state_set.end());
}

} // end namespace cliver
