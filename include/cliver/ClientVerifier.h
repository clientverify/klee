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

#include "cliver/Socket.h" // For SocketEventList typedef
#include "cliver/ExecutionObserver.h"

#include "klee/Interpreter.h"
#include "klee/Statistic.h"
#include "klee/Statistics.h"
#include "klee/TimerStatIncrementer.h"
#include "klee/Internal/ADT/KTest.h"

#include "llvm/Support/CommandLine.h"

#include <boost/signal.hpp>

namespace cliver {

////////////////////////////////////////////////////////////////////////////////

enum CliverMode {
  DefaultMode, 
	TetrinetMode, 
	XpilotMode, 
	DefaultTrainingMode, 
	TestTrainingMode, 
	OutOfOrderTrainingMode, 
	TetrinetTrainingMode,
	XpilotTrainingMode,
	VerifyWithTrainingPaths,
	VerifyWithEditCost
};

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

class CVExecutor;
class CVExecutionState;
class CVSearcher;
class ConstraintPruner;
class InstructionCounter;
class ExecutionEvent;
class ExecutionTreeManager;
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
  ClientVerifier(std::string* input_filename=NULL);
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
	
  // Observers
  void hook(ExecutionObserver* observer);
  void unhook(ExecutionObserver* observer);
  void notify_all(ExecutionEvent ev);
 
	// Accessors (ugly)
	CVSearcher* searcher();
	CVExecutor* executor();

	// Stats
	void handle_statistics();
	void next_statistics();
	void print_current_statistics(std::string prefix);
	void next_round();

	// Arrays
	unsigned next_array_id() { return array_id_++; }
	unsigned round() { return round_number_; }
 
	// Training paths
	int read_training_paths(std::vector<std::string> &filename_list,
			PathManagerSet *path_manager_set);

  CVStream* cvstream() { return cvstream_; }

  std::string& client_name() { return client_name_; }

 private:

  CVStream *cvstream_;
	int paths_explored_;
	std::vector<klee::StatisticRecord*> statistics_;

  CVExecutor *executor_;
  CVSearcher *searcher_;
  ConstraintPruner* pruner_;
	StateMerger* merger_;

  ExecutionTreeManager *execution_tree_manager_;
  InstructionCounter *i_counter_;

	std::vector<SocketEventList*> socket_events_;

  std::list<ExecutionObserver*> observers_;

	unsigned array_id_;
	unsigned round_number_;
  std::string client_name_;
};


} // end namespace cliver

extern cliver::ClientVerifier *g_client_verifier;

#endif // CLIVER_H
