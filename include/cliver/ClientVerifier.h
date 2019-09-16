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
#include <mutex>

#include "cliver/ExecutionObserver.h"
#include "cliver/Socket.h" // For SocketEventList typedef
#include "cliver/Statistics.h"

#include "klee/Interpreter.h"
#include "klee/Statistic.h"
#include "klee/Statistics.h"
#include "klee/TimerStatIncrementer.h"
#include "klee/Internal/ADT/KTest.h"
#include "klee/util/Mutex.h"

#include "llvm/Support/CommandLine.h"

#define TLS_MASTER_SECRET_SIZE 48

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
class SearcherStage;

class ClientVerifier : public klee::InterpreterHandler {
 public:
  //Marie merge: problematic, actually implement these...
  virtual void incRecoveryStatesCount() {};
  virtual void incGeneratedSlicesCount() {};
  virtual void incSnapshotsCount() {};

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

  static klee::Interpreter *create_interpreter(klee::Interpreter::InterpreterOptions &opts,
                                 klee::InterpreterHandler *ih);

	// Initialization
	void initialize();
  void assign_basic_block_ids();
	
	// ExternalHandlers
	void initialize_external_handlers(CVExecutor *executor);
	
	// Socket logs
	int read_socket_logs(std::vector<std::string> &logs);
	std::vector<SocketEventList*>& socket_events() { return socket_events_; }

  // TLS Master Secret (RFC 5246 requires it to be 48 bytes)
  bool load_tls_master_secret(uint8_t master_secret[TLS_MASTER_SECRET_SIZE]);

  // Observers
  void hook(ExecutionObserver* observer);
  void unhook(ExecutionObserver* observer);
  void notify_all(ExecutionEvent ev);
 
	// Accessors
	CVSearcher* searcher();
	CVExecutor* executor();
  ExecutionTraceManager* execution_trace_manager();

  // Write all stats to file
  void write_all_stats();

  // Print the stats for the current round
  void print_current_round_stats();

  // Set global context (round)
  void set_round(int round, SearcherStage* stage);

  int round() { return round_number_; }

	// Arrays
	inline uint64_t next_array_id() { return array_id_++; }

  std::string& client_name() { return client_name_; }

  klee::KBasicBlock* LookupBasicBlockID(int id);

  KTest* get_replay_objs() { return replay_objs_; }

  CVStream* get_cvstream() { return cvstream_; }

  uint64_t get_round_statistic_value(int round, const klee::Statistic &stat);

  // Return status of verifier: 0 == we've found a state consistent with the
  // socket log, non-zero otherwise
  int status();

  void WriteSearcherStageGraph();

  CVStatisticsManager* sm() { return &statistics_manager_; }

  const std::string &socket_log_text_file() { return socket_log_text_file_; }

  bool drop_s2c_tls_appdata() { return drop_s2c_tls_appdata_; }

 private:
  CVStream *cvstream_;
  klee::Atomic<int>::type paths_explored_;
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

  std::string socket_log_text_file_; // optional
  bool drop_s2c_tls_appdata_ = false;

  // RFC 5246 TLS Master Secret (48 bytes)
  klee::Mutex master_secret_mutex_;
  bool master_secret_cached_ = false;
  uint8_t master_secret_[TLS_MASTER_SECRET_SIZE];

  CVStatisticsManager statistics_manager_;
};


} // end namespace cliver

extern cliver::ClientVerifier *g_client_verifier;

#endif // CLIENT_VERIFIER_H
