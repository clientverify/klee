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

#include "ClientVerifier.h" // For CliverEvent::Type
#include "ExecutionStateProperty.h"

#include "../Core/Searcher.h"

namespace cliver {
class CVExecutionState;
class StateMerger;
class PathSet;

////////////////////////////////////////////////////////////////////////////////

class CVSearcher : public klee::Searcher {
 public:
	CVSearcher(klee::Searcher* base_searcher, StateMerger* merger);

	virtual klee::ExecutionState &selectState();

	virtual void update(klee::ExecutionState *current,
							const std::set<klee::ExecutionState*> &addedStates,
							const std::set<klee::ExecutionState*> &removedStates);

	virtual bool empty();

	virtual void printName(std::ostream &os) { os << "CVSearcher\n"; }

	static void handle_pre_event(CVExecutionState *state, 
			CVExecutor *executor, CliverEvent::Type et);
	static void handle_post_event(CVExecutionState *state, 
			CVExecutor *executor, CliverEvent::Type et);

 protected:
	klee::Searcher* base_searcher_;
	StateMerger* merger_;
};

////////////////////////////////////////////////////////////////////////////////

class LogIndexSearcher : public CVSearcher { 
 public:
	LogIndexSearcher(klee::Searcher* base_searcher, StateMerger* merger);

	klee::ExecutionState &selectState();

	void update(klee::ExecutionState *current,
							const std::set<klee::ExecutionState*> &addedStates,
							const std::set<klee::ExecutionState*> &removedStates);

	bool empty();

	void printName(std::ostream &os) { os << "LogIndexSearcher\n"; }

	static void handle_pre_event(CVExecutionState *state, 
			CVExecutor *executor, CliverEvent::Type et);
	static void handle_post_event(CVExecutionState *state, 
			CVExecutor *executor, CliverEvent::Type et);

 protected:
	int state_count();
	ExecutionStatePropertyMap states_;
	klee::Searcher* base_searcher_;
	StateMerger* merger_;
};

////////////////////////////////////////////////////////////////////////////////

class TrainingSearcher : public CVSearcher {
 public:
	TrainingSearcher(klee::Searcher* base_searcher, StateMerger* merger);

	klee::ExecutionState &selectState();

	void update(klee::ExecutionState *current,
							const std::set<klee::ExecutionState*> &addedStates,
							const std::set<klee::ExecutionState*> &removedStates);

	bool empty();

	//void clone_for_network_events(CVExecutionState *state, CVExecutor* executor, 
	//		CliverEvent::Type et);

	void record_path(CVExecutionState *state, CVExecutor* executor,
			CliverEvent::Type et);

	void printName(std::ostream &os) { os << "TrainingSearcher\n"; }

	static void handle_pre_event(CVExecutionState *state, 
			CVExecutor *executor, CliverEvent::Type et);

	static void handle_post_event(CVExecutionState *state, 
			CVExecutor *executor, CliverEvent::Type et);

 private:
	ExecutionStateSet phases_[PathProperty::EndState];
	PathSet *paths_;
};

////////////////////////////////////////////////////////////////////////////////

class VerifySearcher : public CVSearcher {
 public:
	VerifySearcher(klee::Searcher* base_searcher, StateMerger* merger, PathSet *paths);

	klee::ExecutionState &selectState();

	void update(klee::ExecutionState *current,
							const std::set<klee::ExecutionState*> &addedStates,
							const std::set<klee::ExecutionState*> &removedStates);

	bool empty();

	//void clone_for_network_events(CVExecutionState *state, CVExecutor* executor, 
	//		CliverEvent::Type et);

	void end_path(CVExecutionState *state, CVExecutor* executor,
			CliverEvent::Type et);

	void printName(std::ostream &os) { os << "VerifySearcher\n"; }

	static void handle_pre_event(CVExecutionState *state, 
			CVExecutor *executor, CliverEvent::Type et);

	static void handle_post_event(CVExecutionState *state, 
			CVExecutor *executor, CliverEvent::Type et);

 private:
	ExecutionStateSet phases_[PathProperty::EndState];
	ExecutionStatePropertyMap saved_states_;
	PathSet *paths_;
};

//////////////////////////////////////////////////////////////////////////////////
//
//class OutOfOrderTrainingSearcher : public CVSearcher {
// public:
//	OutOfOrderTrainingSearcher(klee::Searcher* base_searcher, StateMerger* merger);
//
//	klee::ExecutionState &selectState();
//
//	void update(klee::ExecutionState *current,
//							const std::set<klee::ExecutionState*> &addedStates,
//							const std::set<klee::ExecutionState*> &removedStates);
//
//	bool empty();
//
//	void printName(std::ostream &os) {
//		os << "OutOfOrderTrainingSearcher\n";
//	}
//
//	void clone_for_network_events(CVExecutionState *state, CVExecutor* executor, 
//			CliverEvent::Type et);
//
//	void record_path(CVExecutionState *state, CVExecutor* executor,
//			CliverEvent::Type et);
//
//	static void handle_pre_event(CVExecutionState *state, 
//			CVExecutor *executor, CliverEvent::Type et);
//	static void handle_post_event(CVExecutionState *state, 
//			CVExecutor *executor, CliverEvent::Type et);
//
// private:
//	ExecutionStateSet phases_[PathProperty::EndState];
//	PathSet *paths_;
//};

} // end namespace cliver
#endif // CV_SEARCHER_H
