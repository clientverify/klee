//===-- ClientVerifier.h ----------------------------------------*- C++ -*-===//
//
// <insert license>
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//
#ifndef CLIENT_VERIFIER_H
#define CLIENT_VERIFIER_H

#include <fstream>
#include <string>
#include <vector>
#include <list>

#include "cliver/ExecutionObserver.h"
#include "cliver/Socket.h" // For SocketEventList typedef
#include "cliver/Statistics.h"

#include "klee/Interpreter.h"
#include "klee/Statistic.h"
#include "klee/Statistics.h"
#include "klee/TimerStatIncrementer.h"
#include "klee/Internal/ADT/KTest.h"

#include "llvm/Support/CommandLine.h"

namespace klee {
  struct KBasicBlock;
}

namespace cliver {

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
  VerifyEditDistanceKPrefixTest,
  VerifyJaccard,
  VerifyMultiSetJaccard,
  VerifyMultiSetJaccardPrefix,
  VerifyApproxEditDistance
};

enum ClientModelType {
  Tetrinet,
  XPilot,
  Simple
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
  ClientVerifier(std::string &input_file, bool no_output, std::string &output_dir);
  ~ClientVerifier();
	
	// klee::InterpreterHandler
  llvm::raw_ostream &getInfoStream() const;
  std::string getOutputFilename(const std::string &filename);
  llvm::raw_fd_ostream *openOutputFile(const std::string &filename);
  llvm::raw_fd_ostream *openOutputFileInSubDirectory(const std::string &filename,
                                             const std::string &sub_directory);
	void getFiles(std::string path, std::string suffix,
                std::vector<std::string> &results);

	void getFilesRecursive(std::string path, std::string suffix,
                         std::vector<std::string> &results);
  void incPathsExplored();
  void processTestCase(const klee::ExecutionState &state, 
                       const char *err, const char *suffix);

  void setInterpreter(klee::Interpreter *i);

  static klee::Interpreter *create_interpreter(const klee::Interpreter::InterpreterOptions &opts,
                                 klee::InterpreterHandler *ih);

	// Initialization
	void initialize();
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
 
	// Accessors
	CVSearcher* searcher();
	CVExecutor* executor();
  ExecutionTraceManager* execution_trace_manager();

  // Write all stats to file
  void print_all_stats();

  // Set global context (round)
  void set_round(int round);

  int round() { return round_number_; }

	// Arrays
	inline uint64_t next_array_id() { return array_id_++; }

  std::string& client_name() { return client_name_; }

  klee::KBasicBlock* LookupBasicBlockID(int id);

  KTest* get_replay_objs() { return replay_objs_; }

  CVStream* get_cvstream() { return cvstream_; }

  uint64_t get_round_statistic_value(int round, const klee::Statistic &stat);

 private:
  CVStream *cvstream_;
	int paths_explored_;
  std::map<int,klee::KBasicBlock*> basicblock_map_;

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

  KTest* replay_objs_;

  CVStatisticsManager statistics_manager_;
};


} // end namespace cliver

extern cliver::ClientVerifier *g_client_verifier;

#endif // CLIENT_VERIFIER_H
