//===-- ConstraintPruner.cpp ------------------------------------*- C++ -*-===//
//
// <insert license>
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//

#include "cliver/AddressSpaceGraph.h"
#include "cliver/ClientVerifier.h"
#include "cliver/ConstraintPruner.h"
#include "cliver/CVExecutionState.h"
#include "cliver/CVStream.h"
#include "CVCommon.h"

#include "klee/IndependentElementSet.h"
#include "klee/Constraints.h"

#include "llvm/Support/CommandLine.h"

namespace cliver {

llvm::cl::opt<bool>
DebugConstraintPruner("debug-constraint-pruner",llvm::cl::init(false));

#ifndef NDEBUG

#undef CVDEBUG
#define CVDEBUG(x) \
	__CVDEBUG(DebugConstraintPruner, x);

#undef CVDEBUG_S
#define CVDEBUG_S(__state_id, __x) \
	__CVDEBUG_S(DebugConstraintPruner, __state_id, __x)

#else

#undef CVDEBUG
#define CVDEBUG(x)

#undef CVDEBUG_S
#define CVDEBUG_S(__state_id, __x)

#endif


ConstraintPruner::ConstraintPruner() {}

void ConstraintPruner::prune_constraints(
		CVExecutionState &state, AddressSpaceGraph &graph ) {

	klee::IndependentElementSet array_set;
	foreach (const klee::Array* array, graph.arrays()) {
		array_set.addArray(array);
	}

  std::vector< klee::ref<klee::Expr> > result;
  std::vector< klee::ref<klee::Expr> > removed_constraints;
  std::vector< std::pair<klee::ref<klee::Expr>, klee::IndependentElementSet> > worklist;

  for (klee::ConstraintManager::const_iterator it = state.constraints.begin(), 
         ie =state.constraints.end(); it != ie; ++it)
    worklist.push_back(std::make_pair(*it, klee::IndependentElementSet(*it)));

	int start_size = worklist.size();
  bool done = false;
  do {
    done = true;
    std::vector< std::pair<klee::ref<klee::Expr>, klee::IndependentElementSet> > newWorklist;
    for (std::vector< std::pair<klee::ref<klee::Expr>, klee::IndependentElementSet> >::iterator
           it = worklist.begin(), ie = worklist.end(); it != ie; ++it) {
      if (it->second.intersects(array_set)) {
        if (array_set.add(it->second))
          done = false;
        result.push_back(it->first);
      } else {
        newWorklist.push_back(*it);
      }
    }
    worklist.swap(newWorklist);
  } while (!done);

	for (std::vector< std::pair<klee::ref<klee::Expr>, klee::IndependentElementSet> >::iterator
					it = worklist.begin(), ie = worklist.end(); it != ie; ++it) {
		CVDEBUG("Removed: " << it->first );
	}
	CVDEBUG_S(state.id(), "removed " << start_size - result.size() << " constraints");

	state.constraints = klee::ConstraintManager(result);
}

} // end namespace cliver
