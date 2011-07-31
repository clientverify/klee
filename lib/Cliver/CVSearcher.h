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

namespace cliver {

class CVExecutionState;

class CVSearcher : public klee::Searcher {
 public:
	CVSearcher(klee::Searcher *base_searcher);

	klee::ExecutionState &selectState();

	void update(klee::ExecutionState *current,
							const std::set<klee::ExecutionState*> &addedStates,
							const std::set<klee::ExecutionState*> &removedStates);

	bool empty() { return base_searcher_->empty(); /*return states_.empty();*/ }

	void printName(std::ostream &os) {
		os << "CVSearcher\n";
	}
 private:
	std::vector<CVExecutionState*> states_;
	klee::Searcher* base_searcher_;
};

} // end namespace cliver
#endif // CV_SEARCHER_H
