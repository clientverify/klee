//===-- ConstraintPruner.cpp ------------------------------------*- C++ -*-===//
//
// <insert license>
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//

#include "AddressSpaceGraph.h"
#include "ConstraintPruner.h"
#include "CVExecutionState.h"
#include "klee/IndependentElementSet.h"
#include "klee/Constraints.h"
#include "CVStream.h"

namespace cliver {

ConstraintPruner::ConstraintPruner() {}

void ConstraintPruner::prune( CVExecutionState &state, AddressSpaceGraph &graph ) {

	klee::IndependentElementSet arrays(graph.arrays());
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
      if (it->second.intersects(arrays)) {
        if (arrays.add(it->second))
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
		*cv_debug_stream << "Removed: " << it->first << "\n";
	}
	CVDEBUG_S(state.id(), "removed " << start_size - result.size() << " constraints");

	state.constraints = klee::ConstraintManager(result);
}

} // end namespace cliver
