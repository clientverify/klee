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
class StateMerger;

////////////////////////////////////////////////////////////////////////////////

class CVSearcher : public klee::Searcher {
 public:
	CVSearcher(klee::Searcher* base_searcher, StateMerger* merger);

	klee::ExecutionState &selectState();

	void update(klee::ExecutionState *current,
							const std::set<klee::ExecutionState*> &addedStates,
							const std::set<klee::ExecutionState*> &removedStates);

	bool empty();

	void printName(std::ostream &os) {
		os << "CVSearcher\n";
	}
 protected:
	int state_count();
	ExecutionStatePropertyMap states_;
	ExecutionStateProperty* current_property_;
	klee::Searcher* base_searcher_;
	StateMerger* merger_;
};

////////////////////////////////////////////////////////////////////////////////

class TrainingSearcher : public CVSearcher {
 public:
	TrainingSearcher(klee::Searcher* base_searcher, StateMerger* merger);

	klee::ExecutionState &selectState();
	void printName(std::ostream &os) {
		os << "TrainingSearcher\n";
	}
 private:
	TrainingPhaseProperty* phases_[MAX_TRAINING_PHASE];
};

////////////////////////////////////////////////////////////////////////////////

} // end namespace cliver
#endif // CV_SEARCHER_H
