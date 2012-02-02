//===-- ClientVerifier.h ----------------------------------------*- C++ -*-===//
//
// <insert license>
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//
#ifndef CLIVER_H
#define CLIVER_H

#include <fstream>
#include <map>
#include <string>
#include <vector>
#include <list>

#include "Socket.h" // For SocketEventList typedef
#include "ExecutionObserver.h"

#include "klee/TimerStatIncrementer.h"
#include "klee/Internal/ADT/KTest.h"
#include "../lib/Core/CoreStats.h"
#include "../lib/Core/SpecialFunctionHandler.h"
#include "../Core/Executor.h"

#include "llvm/Support/CommandLine.h"

#include <boost/signal.hpp>

namespace cliver {

////////////////////////////////////////////////////////////////////////////////

enum CliverMode {
  DefaultMode, 
	TetrinetMode, 
	XpilotMode, 
	DefaultTrainingMode, 
	OutOfOrderTrainingMode, 
	TetrinetTrainingMode,
	XpilotTrainingMode,
	VerifyWithTrainingPaths,
	VerifyWithEditCost
};

extern llvm::cl::opt<CliverMode> g_cliver_mode;

////////////////////////////////////////////////////////////////////////////////

namespace stats {
	extern klee::Statistic active_states;
	extern klee::Statistic merged_states;
	extern klee::Statistic round_time;
	extern klee::Statistic round_real_time;
	extern klee::Statistic merge_time;
	extern klee::Statistic prune_time;
	extern klee::Statistic pruned_constraints;
	extern klee::Statistic searcher_time;
	extern klee::Statistic fork_time;
	extern klee::Statistic training_paths;
	extern klee::Statistic exhaustive_search_level;
}

////////////////////////////////////////////////////////////////////////////////

void ExternalHandler_nop (klee::Executor* executor, klee::ExecutionState *state, 
		klee::KInstruction *target, std::vector<klee::ref<klee::Expr> > &arguments);

struct ExternalHandlerInfo {
	const char* name;
	klee::SpecialFunctionHandler::ExternalHandler handler;
	bool has_return_value;
  ExecutionEventType event_triggered;
};

////////////////////////////////////////////////////////////////////////////////

class CVExecutor;
class CVExecutionState;
class CVSearcher;
class ConstraintPruner;
class ExecutionEvent;
class ExecutionTree;
class StateMerger;
class PathManager;
class PathManagerSet;

class CVContext {
 public:
  CVContext();
  int id() {return context_id_;}
 private:
  int increment_id() { return next_id_++; }

  int context_id_;
  static int next_id_;
};

////////////////////////////////////////////////////////////////////////////////

class CVStream;

class ClientVerifier : public klee::InterpreterHandler {
 public:
	//typedef 
	//	boost::function<void (CVExecutionState*, CVExecutor*, CliverEvent::Type)> 
	//	callback_func_ty;
	//typedef 
	//	boost::signal<void (CVExecutionState*, CVExecutor*, CliverEvent::Type)> 
	//	signal_ty;

  ClientVerifier();
  virtual ~ClientVerifier();
	
	// klee::InterpreterHandler
  std::ostream &getInfoStream() const;
  std::string getOutputFilename(const std::string &filename);
  std::ostream *openOutputFile(const std::string &filename);
  void incPathsExplored();
  void processTestCase(const klee::ExecutionState &state, 
                       const char *err, const char *suffix);
	// Initialization
	void initialize(CVExecutor *executor);
	
	// ExternalHandlers
	void initialize_external_handlers(CVExecutor *executor);
	
	// Socket logs
	int read_socket_logs(std::vector<std::string> &logs);
	std::vector<SocketEventList*>& socket_events() { return socket_events_; }
	
	// Events
	//void register_events(CVExecutor *executor);
	//void pre_event(CVExecutionState* state, 
	//		CVExecutor* executor, CliverEvent::Type t); 
	//void post_event(CVExecutionState* state,
	//		CVExecutor* executor, CliverEvent::Type t); 

  // Observers
  void hook(ExecutionObserver* observer);
  void unhook(ExecutionObserver* observer);
  void notify_all(ExecutionEvent ev);
 
	// Searcher
	CVSearcher* searcher();

	// Stats
	void handle_statistics();
	void next_statistics();
	void print_current_statistics();

	// Arrays
	unsigned next_array_id() { return array_id_++; }
 
	// Training paths
	int read_training_paths(std::vector<std::string> &filename_list,
			PathManagerSet *path_manager_set);

 private:

  CVStream *cvstream_;
	int paths_explored_;
	std::vector<klee::StatisticRecord*> statistics_;

  CVSearcher *searcher_;
  ConstraintPruner* pruner_;
	StateMerger* merger_;

  ExecutionTree *current_execution_tree_;

	//signal_ty pre_event_callbacks_;
	//signal_ty post_event_callbacks_;
	//callback_func_ty pre_event_callback_func_;
	//callback_func_ty post_event_callback_func_;

	std::vector<SocketEventList*> socket_events_;

  std::list<ExecutionObserver*> observers_;

	unsigned array_id_;
};


} // end namespace cliver

extern cliver::ClientVerifier *g_client_verifier;

#endif // CLIVER_H
