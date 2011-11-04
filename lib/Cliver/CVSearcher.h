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
class PathManagerSet;

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
	ExecutionStateSet phases_[PathProperty::EndPhase];
	PathManagerSet *paths_;
};

////////////////////////////////////////////////////////////////////////////////

class PathSelector;

/// Each VerifyStage object holds ExecutionStates, where all the states in a 
/// given VerifyStage have processed network events 1 to i, where i is equal
/// among all the states. Each VerifyStage has a single root state from which
/// all of the other states began execution.
class VerifyStage {
 public:
	typedef enum { FullTraining, ConcreteTraining, PrefixTraining, Exhaustive } SearchMode;

	VerifyStage(PathSelector *path_selector, const SocketEvent* socket_event, 
			VerifyStage* parent=NULL);
	CVExecutionState* next_state();
	void add_state(CVExecutionState *state);
	void remove_state(CVExecutionState *state);
	void finish(CVExecutionState *state);

	ExecutionStateSet& finished_states() { return finished_states_; }
	std::vector<VerifyStage*>& children() { return children_; }

 private: 
	// Root state from which all other states began execution
	CVExecutionState *root_state_;
	// Used when cloning root_state_ to assign a new PathManager to explore
	PathSelector *path_selector_;
  // States that are currently ready to continue executions
	ExecutionStateSet states_; 
  // States that are not active
	ExecutionStateSet deactivated_states_;
  // States that have finished execution
  ExecutionStateSet finished_states_; 
  // The socket event that this stage is associated with
	const SocketEvent* socket_event_; 
	// Network event index
	unsigned network_event_index_;
	// Parent 
	const VerifyStage* parent_;
	// Children of this VerifyStage
	std::vector<VerifyStage*> children_;

	SearchMode search_mode;

};

class VerifySearcher : public CVSearcher {
 public:
	VerifySearcher(klee::Searcher* base_searcher, StateMerger* merger, PathManagerSet *paths);

	klee::ExecutionState &selectState();

	void update(klee::ExecutionState *current,
							const std::set<klee::ExecutionState*> &addedStates,
							const std::set<klee::ExecutionState*> &removedStates);

	bool empty();

	void end_path(CVExecutionState *state, CVExecutor* executor,
			CliverEvent::Type et);

	void printName(std::ostream &os) { os << "VerifySearcher\n"; }

	static void handle_pre_event(CVExecutionState *state, 
			CVExecutor *executor, CliverEvent::Type et);

	static void handle_post_event(CVExecutionState *state, 
			CVExecutor *executor, CliverEvent::Type et);

 private:
	PathSelector *path_selector_;
	VerifyStage *root_stage_;
	VerifyStage *current_stage_;
};

} // end namespace cliver
#endif // CV_SEARCHER_H
