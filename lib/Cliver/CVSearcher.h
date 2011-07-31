//===-- CVSearcher.h --------------------------------------------*- C++ -*-===//
//
// <insert license>
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//
#ifndef CV_SEARCHER_H
#define CV_SEARCHER_H

#include "../Core/Searcher.h"
#include "CVExecutionState.h"

namespace cliver {

typedef std::set<CVExecutionState*> ExecutionStateSet;

typedef std::map<ExecutionStateInfo, 
								 ExecutionStateSet,
								 ExecutionStateInfoLT> ExecutionStateMap;


class CVSearcher : public klee::Searcher {
 public:
	CVSearcher(klee::Searcher *base_searcher);

	klee::ExecutionState &selectState();

	void update(klee::ExecutionState *current,
							const std::set<klee::ExecutionState*> &addedStates,
							const std::set<klee::ExecutionState*> &removedStates);

	bool empty();

	void printName(std::ostream &os) {
		os << "CVSearcher\n";
	}
 private:
	int state_count();
	ExecutionStateMap states_;
	klee::Searcher* base_searcher_;
};

} // end namespace cliver
#endif // CV_SEARCHER_H
