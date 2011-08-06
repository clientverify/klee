//===-- StateMerger.cpp -----------------------------------------*- C++ -*-===//
//
// <insert license>
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//

#include "CVSearcher.h"
#include "StateMerger.h"
#include "AddressSpaceGraph.h"
#include "ConstraintPruner.h"
#include "ClientVerifier.h"

namespace cliver {

llvm::cl::opt<bool>
DebugStateMerger("debug-state-merger",llvm::cl::init(false));

#ifndef NDEBUG

#undef CVDEBUG
#define CVDEBUG(x) \
	__CVDEBUG(DebugStateMerger, x);

#undef CVDEBUG_S
#define CVDEBUG_S(__state_id, __x) \
	__CVDEBUG_S(DebugStateMerger, __state_id, __x)

#else

#undef CVDEBUG
#define CVDEBUG(x)

#undef CVDEBUG_S
#define CVDEBUG_S(__state_id, __x)

#endif

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

bool StateMerger::compare_constraints(
		const AddressSpaceGraph &asg_a,
		const AddressSpaceGraph &asg_b,
		klee::ConstraintManager &a, 
		klee::ConstraintManager &b) {

	std::set< klee::ref<klee::Expr> > set_a(a.begin(), a.end());
	std::set< klee::ref<klee::Expr> > set_b_initial(b.begin(), b.end());

	if (set_a.size() != set_b_initial.size()) {
		CVDEBUG("constraint sizes do not match " 
				<< set_a.size() << " != " << set_b_initial.size());
		return false;
	}

	std::set< klee::ref<klee::Expr> > set_b;

	foreach (klee::ref<klee::Expr> e, set_b_initial) {
		set_b.insert(asg_a.get_canonical_expr(asg_b, e));
	}

	std::set< klee::ref<klee::Expr> > common;

	std::set_intersection(set_a.begin(), set_a.end(), set_b.begin(), set_b.end(),
			std::inserter(common, common.begin()));

	if (common.size() == set_a.size() &&
			common.size() == set_b.size()) {
		return true;
	} else {
		CVDEBUG("constraints do not match");
		foreach( klee::ref<klee::Expr> e, set_a) {
			if (!common.count(e)) {
				CVDEBUG("(1) " << e);
			}
		}
		foreach( klee::ref<klee::Expr> e, set_b) {
			if (!common.count(e)) {
				CVDEBUG("(2) " << e);
			}
		}
		return false;
	}
}

void StateMerger::merge(ExecutionStateSet &state_set, 
		ExecutionStateSet &merged_set) {
	klee::TimerStatIncrementer timer(stats::merge_time);

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
			AddressSpaceGraph* asg_a = merge_info[state].graph;
			AddressSpaceGraph* asg_b = merge_info[*it].graph;
			if (asg_a->equals(*asg_b)) {
				if (compare_constraints(*asg_a, *asg_b,
							state->constraints, (*it)->constraints)) {
					break;
				}
			}
		}
		if (it == ie) {
			unique_states.push_back(state);
			merged_set.insert(state);
		}

  } while (!worklist.empty());

	std::map<CVExecutionState*, MergeInfo>::iterator it=merge_info.begin(),
		ie=merge_info.end();
	for (;it!=ie; ++it) {
		delete (it->second).graph;
	}

	CVDEBUG("Found " << state_set.size() - unique_states.size() 
			<< " duplicates out of " << state_set.size() 
			<< ", now " << unique_states.size() << " states remain.");
}

} // end namespace cliver
