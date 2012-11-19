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

namespace cliver {

////////////////////////////////////////////////////////////////////////////////

namespace stats {
	extern klee::Statistic round_number;
	extern klee::Statistic round_time;
	extern klee::Statistic round_real_time;
	extern klee::Statistic round_sys_time;
	extern klee::Statistic merge_time;
	extern klee::Statistic searcher_time;
	extern klee::Statistic rebuild_time;
	extern klee::Statistic execution_tree_time;
	extern klee::Statistic edit_distance_time;
	extern klee::Statistic edit_distance_build_time;
	extern klee::Statistic edit_distance_hint_time;
	extern klee::Statistic edit_distance_stat_time;
	extern klee::Statistic round_instructions;
	extern klee::Statistic recv_round_instructions;
	extern klee::Statistic merged_states;
	extern klee::Statistic stage_count;
	extern klee::Statistic state_clone_count;
	extern klee::Statistic state_remove_count;
	extern klee::Statistic edit_distance;
	extern klee::Statistic edit_distance_k;
	extern klee::Statistic edit_distance_medoid_count;
	extern klee::Statistic edit_distance_self_first_medoid;
	extern klee::Statistic edit_distance_self_last_medoid;
	extern klee::Statistic edit_distance_self_socket_event;
	extern klee::Statistic edit_distance_socket_event_first_medoid;
	extern klee::Statistic edit_distance_socket_event_last_medoid;
	extern klee::Statistic socket_event_size;
	extern klee::Statistic valid_path_instructions;
	extern klee::Statistic symbolic_variable_count;
}

////////////////////////////////////////////////////////////////////////////////

class CVExecutor;
class CVExecutionState;
class CVSearcher;
class ConstraintPruner;
class ExecutionEvent;
class ExecutionTraceManager;
class StateMerger;

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

enum RunModeType {
  Training,
  VerifyNaive,
  VerifyEditDistanceRow,
  VerifyEditDistanceKPrefixRow,
  VerifyEditDistanceKPrefixHash,
  VerifyEditDistanceKPrefixHashPointer,
  VerifyEditDistanceKPrefixTest
};

enum ClientModelType {
  Tetrinet,
  XPilot
};
// stored in Configuration.cpp
extern ClientModelType ClientModelFlag; 

enum SearchModeType {
  Random,
  PriorityQueue,
  DepthFirst,
  BreadthFirst
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
  std::ostream* openOutputFile(const std::string &filename);
  std::ostream* openOutputFileInSubDirectory(const std::string &filename, 
                                             const std::string &sub_directory);
	void getFiles(std::string path, std::string suffix,
                std::vector<std::string> &results);

	void getFilesRecursive(std::string path, std::string suffix,
                         std::vector<std::string> &results);
  void incPathsExplored();
  void processTestCase(const klee::ExecutionState &state, 
                       const char *err, const char *suffix);
	// Initialization
	void initialize(CVExecutor *executor);
  void assign_basic_block_ids();
	
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
  ExecutionTraceManager* execution_trace_manager();

	// Stats
	void update_time_statistics();
	void print_current_statistics(std::string prefix);
  void print_statistic_record(klee::StatisticRecord* sr,
                              std::string &prefix);
	void print_stat_labels();
  void print_current_stats_and_reset();
	void print_all_stats();
	void set_round(int round);
  int round() { return round_number_; }

	// Arrays
	inline uint64_t next_array_id() { return array_id_++; }

  std::string& client_name() { return client_name_; }

  // Flags
  void set_execution_event_flag(bool v = true) { execution_event_flag_ = v; }
  bool execution_event_flag() { return execution_event_flag_; }

 private:
  CVStream *cvstream_;
	int paths_explored_;
	std::vector<klee::StatisticRecord*> statistics_;

  CVExecutor *executor_;
  CVSearcher *searcher_;
  ConstraintPruner* pruner_;
	StateMerger* merger_;

  ExecutionTraceManager *execution_trace_manager_;

	std::vector<SocketEventList*> socket_events_;

  std::list<ExecutionObserver*> observers_;

	uint64_t array_id_;
	int round_number_;
  std::string client_name_;
  // This flag is set if an execution event occurs and is cleared each time we
  // start execution of a new instruction
  bool execution_event_flag_; 
};


} // end namespace cliver

extern cliver::ClientVerifier *g_client_verifier;

#endif // CLIVER_H
