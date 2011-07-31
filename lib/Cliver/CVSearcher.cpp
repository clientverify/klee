//===-- CVSearcher.cpp ------------------------------------------*- C++ -*-===//
//
// <insert license>
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//

#include "CVSearcher.h"

namespace cliver {

CVSearcher::CVSearcher(klee::Searcher* base_searcher) 
	: base_searcher_(base_searcher) {
}

klee::ExecutionState &CVSearcher::selectState() {
	return base_searcher_->selectState();
}

void CVSearcher::update(klee::ExecutionState *current,
						const std::set<klee::ExecutionState*> &addedStates,
						const std::set<klee::ExecutionState*> &removedStates) {
	return base_searcher_->update(current, addedStates, removedStates);
}


} // end namespace cliver
