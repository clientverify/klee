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

#include "klee/TimerStatIncrementer.h"
#include "klee/Internal/ADT/KTest.h"
#include "CVStream.h"
#include "../lib/Core/CoreStats.h"
#include "../Core/Executor.h"

#include <fstream>
#include <map>
#include <string>
#include <vector>

namespace cliver {

////////////////////////////////////////////////////////////////////////////////

namespace stats {
	extern klee::Statistic active_states;
	extern klee::Statistic merged_states;
	extern klee::Statistic round_time;
	extern klee::Statistic round_real_time;
	extern klee::Statistic merge_time;
	extern klee::Statistic prune_time;
	extern klee::Statistic pruned_constraints;
}

////////////////////////////////////////////////////////////////////////////////

class CVExecutor;

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

class ClientVerifier : public klee::InterpreterHandler {
 public:
  ClientVerifier();
  virtual ~ClientVerifier();
  CVStream* getCVStream() { return cvstream_; };
  void init();
	void prepare_to_run(CVExecutor *executor);
	void load_socket_files();
	void initialize_external_handlers(CVExecutor *executor);
	void handle_statistics();
	void update_time_statistics();
	void print_current_statistics();
	std::vector<KTest*> socket_logs() { return socket_logs_; }

  std::ostream &getInfoStream() const;
  std::string getOutputFilename(const std::string &filename);
  std::ostream *openOutputFile(const std::string &filename);
  void incPathsExplored();
  void processTestCase(const klee::ExecutionState &state, 
                       const char *err, const char *suffix);
 
 private:
	int load_socket_logs();

  CVStream *cvstream_;
	std::vector<KTest*> socket_logs_;
	int paths_explored_;
	std::vector<klee::StatisticRecord*> statistics_;
};


} // end namespace cliver

extern cliver::ClientVerifier *g_client_verifier;

#endif // CLIVER_H
