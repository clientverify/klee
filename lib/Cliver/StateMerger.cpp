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

llvm::cl::opt<bool>
TrainingClearConstraints("training-clear-constraints",llvm::cl::init(true));

#ifndef NDEBUG

#undef CVDEBUG
#define CVDEBUG(x) \
	__CVDEBUG(DebugStateMerger, x);

#undef CVDEBUG_S
#define CVDEBUG_S(__state_id, __x) \
	__CVDEBUG_S(DebugStateMerger, __state_id, __x)

#undef CVDEBUG_S2
#define CVDEBUG_S2(__state_id_1, __state_id_2, __x) \
	__CVDEBUG_S2(DebugStateMerger, __state_id_1, __state_id_2, __x) \

#else

#undef CVDEBUG
#define CVDEBUG(x)

#undef CVDEBUG_S
#define CVDEBUG_S(__state_id, __x)

#undef CVDEBUG_S2
#define CVDEBUG_S2(__state_id_1, __state_id_2, __x)

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

bool StateMerger::callstacks_equal(CVExecutionState *state_a, 
		CVExecutionState *state_b) {

	int id_a = state_a->id(), id_b = state_b->id();

	std::vector<klee::StackFrame>::const_iterator itA = state_a->stack.begin();
	std::vector<klee::StackFrame>::const_iterator itB = state_b->stack.begin();

	if (state_a->pc != state_b->pc) {
		CVDEBUG_S2(id_a, id_b, "pc instruction doesn't match");
		return false;
	}

	while (itA!=state_a->stack.end() && itB!=state_b->stack.end()) {
		if (itA->caller!=itB->caller || itA->kf!=itB->kf) {
			std::string a_caller_str, b_caller_str, a_kf_str, b_kf_str;
			util_inst_string(itA->caller->inst, a_caller_str);
			util_inst_string(itB->caller->inst, b_caller_str);
			CVDEBUG_S2(id_a, id_b, "call stacks don't match" 
					<< a_caller_str << b_caller_str);
			return false;
		}
		++itA;
		++itB;
	}

	if (itA!=state_a->stack.end() || itB!=state_b->stack.end()) {
		CVDEBUG_S2(id_a, id_b, "stack sizes don't match");
		return false;
	}

	return true;
}

bool StateMerger::constraints_equal(
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

	if (common.size() != set_a.size() ||
			common.size() != set_b.size()) {

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

	return true;
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

			// Compare callstacks
			if (!callstacks_equal(state, *it)) {
				continue;
			}

			// Compare address space structure
			if (!asg_a->equal(*asg_b)) {
				continue;
			}

			// Compare rewritten/canonical constraints
			if (constraints_equal(*asg_a, *asg_b, 
						state->constraints, (*it)->constraints)) {
					break;
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

	// Delete AddressSpaceGraph objects
	std::map<CVExecutionState*, MergeInfo>::iterator it=merge_info.begin(),
		ie=merge_info.end();
	for (;it!=ie; ++it) {
		delete (it->second).graph;
	}

}

////////////////////////////////////////////////////////////////////////////////

SymbolicStateMerger::SymbolicStateMerger(ConstraintPruner *pruner) 
	: StateMerger(pruner) {}

void SymbolicStateMerger::merge(ExecutionStateSet &state_set, 
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

  do {
		CVExecutionState* state = worklist.back();
		worklist.pop_back();
		std::set<klee::ObjectState*> symbolic_objs;

		std::vector<CVExecutionState*> new_worklist;
		std::vector<CVExecutionState*>::iterator 
			it=worklist.begin(), ie=worklist.end();
		// Collect non-equal states into new_worklist
		for (; it!=ie; ++it) {
			AddressSpaceGraph* asg_a = merge_info[state].graph;
			AddressSpaceGraph* asg_b = merge_info[*it].graph;

			// Compare callstacks
			if (!callstacks_equal(state, *it)) {
				new_worklist.push_back(*it);
				continue;
			}

			// Compare address space structure
			if (!asg_a->symbolic_equal(*asg_b, symbolic_objs)) {
				new_worklist.push_back(*it);
				continue;
			}

			if (!TrainingClearConstraints) {
				if (!constraints_equal(*asg_a, *asg_b, 
							state->constraints, (*it)->constraints)) {
					new_worklist.push_back(*it);
					continue;
				}
			}
		}

		// Make non-equal objects symbolic
		if (!symbolic_objs.empty()) {

			foreach (klee::ObjectState* obj, symbolic_objs) {
				CVDEBUG("Making object state symbolic: " << *obj);
				const klee::MemoryObject* mo = obj->getObject();
				unsigned id = g_client_verifier->next_array_id();
				const klee::Array *array 
					= new klee::Array(mo->name + llvm::utostr(id), mo->size);

				klee::ObjectState *os = new klee::ObjectState(mo, array);
				state->addressSpace.bindObject(mo, os);
				state->addSymbolic(mo, array);
			}
		}

		if (TrainingClearConstraints) {
			state->constraints.clear();
		}

		merged_set.insert(state);

		worklist.swap(new_worklist);

  } while (!worklist.empty());

	CVDEBUG("Found " << state_set.size() - merged_set.size() 
			<< " duplicates out of " << state_set.size() 
			<< ", now " << merged_set.size() << " states remain.");

	// Delete AddressSpaceGraph objects
	std::map<CVExecutionState*, MergeInfo>::iterator it=merge_info.begin(),
		ie=merge_info.end();
	for (;it!=ie; ++it) {
		delete (it->second).graph;
	}
}

} // end namespace cliver
