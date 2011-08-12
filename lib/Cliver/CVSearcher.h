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
#include "PathManager.h"

namespace cliver {
class StateMerger;

////////////////////////////////////////////////////////////////////////////////

class CVSearcher : public klee::Searcher {
 public:
	CVSearcher(klee::Searcher* base_searcher, StateMerger* merger);

	virtual klee::ExecutionState &selectState();

	virtual void update(klee::ExecutionState *current,
							const std::set<klee::ExecutionState*> &addedStates,
							const std::set<klee::ExecutionState*> &removedStates);

	virtual bool empty();

	virtual void printName(std::ostream &os) {
		os << "CVSearcher\n";
	}

 protected:
	int state_count();
	ExecutionStateSet added_states_;
	ExecutionStateSet removed_states_;
	ExecutionStatePropertyMap states_;
	ExecutionStateProperty* current_property_;
	klee::Searcher* base_searcher_;
	StateMerger* merger_;
};

////////////////////////////////////////////////////////////////////////////////

class LogIndexSearcher : public CVSearcher {
 public:
	LogIndexSearcher(klee::Searcher* base_searcher, StateMerger* merger);

	virtual void printName(std::ostream &os) {
		os << "LogIndexSearcher\n";
	}

	static void handle_pre_event(CVExecutionState *state, 
			CVExecutor *executor, CliverEvent::Type et);
	static void handle_post_event(CVExecutionState *state, 
			CVExecutor *executor, CliverEvent::Type et);
};

////////////////////////////////////////////////////////////////////////////////

class TrainingSearcher : public CVSearcher {
 public:
	TrainingSearcher(klee::Searcher* base_searcher, StateMerger* merger);

	klee::ExecutionState &selectState();

	void update(klee::ExecutionState *current,
							const std::set<klee::ExecutionState*> &addedStates,
							const std::set<klee::ExecutionState*> &removedStates);

	void clone_for_network_events(CVExecutionState *state, CVExecutor* executor);
	void record_path(CVExecutionState *state, CVExecutor* executor);

	void printName(std::ostream &os) {
		os << "TrainingSearcher\n";
	}

	static void handle_pre_event(CVExecutionState *state, 
			CVExecutor *executor, CliverEvent::Type et);
	static void handle_post_event(CVExecutionState *state, 
			CVExecutor *executor, CliverEvent::Type et);

 private:
	ExecutionStateSet phases_[TrainingProperty::EndState];
	PathSet paths_;
};

////////////////////////////////////////////////////////////////////////////////

} // end namespace cliver
#endif // CV_SEARCHER_H
