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
#include "../lib/Core/SpecialFunctionHandler.h"
#include "../Core/Executor.h"
#include "Socket.h"

#include <fstream>
#include <map>
#include <string>
#include <vector>

#include <boost/signal.hpp>

namespace cliver {

////////////////////////////////////////////////////////////////////////////////

enum CliverMode {
  DefaultMode, 
	TetrinetMode, 
	XpilotMode, 
	DefaultTrainingMode, 
	TetrinetTrainingMode,
	XpilotTrainingMode
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
}

////////////////////////////////////////////////////////////////////////////////

void ExternalHandler_nop (klee::Executor* executor, klee::ExecutionState *state, 
		klee::KInstruction *target, std::vector<klee::ref<klee::Expr> > &arguments);

struct ExternalHandlerInfo {
	const char* name;
	klee::SpecialFunctionHandler::ExternalHandler handler;
	bool has_return_value;
};

////////////////////////////////////////////////////////////////////////////////

class CliverEvent { 
	public:
	enum Type {Network, NetworkSend, NetworkRecv, Training};
};

struct CliverEventInfo {
	CliverEvent::Type type;
	int opcode;
	char* function_name;
};

////////////////////////////////////////////////////////////////////////////////

class CVExecutor;
class CVExecutionState;
class CVSearcher;
class ConstraintPruner;
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

class ClientVerifier : public klee::InterpreterHandler {
 public:
	typedef boost::signal<void (CVExecutionState*, CVExecutor*, CliverEvent::Type) > signal_ty;

  ClientVerifier();
  virtual ~ClientVerifier();
	
	// klee::InterpreterHandler
  std::ostream &getInfoStream() const;
  std::string getOutputFilename(const std::string &filename);
  std::ostream *openOutputFile(const std::string &filename);
  void incPathsExplored();
  void processTestCase(const klee::ExecutionState &state, 
                       const char *err, const char *suffix);
	// ExternalHandlers
	void initialize_external_handlers(CVExecutor *executor);
	
	// Socket logs
	void initialize_sockets();
	int read_socket_logs(std::vector<std::string> &logs);
	std::vector<SocketEventList*>& socket_events() { return socket_events_; }
	
	// Events
	void register_events(CVExecutor *executor);
	void pre_event(CVExecutionState* state, 
			CVExecutor* executor, CliverEvent::Type t); 
	void post_event(CVExecutionState* state,
			CVExecutor* executor, CliverEvent::Type t); 

	// Searcher
	CVSearcher* construct_searcher();
	CVSearcher* searcher() { return searcher_; }

	// Stats
	void handle_statistics();
	void update_time_statistics();
	void print_current_statistics();

	// Arrays
	unsigned next_array_id() { return array_id_++; }
 
 private:

  CVStream *cvstream_;
	int paths_explored_;
	std::vector<klee::StatisticRecord*> statistics_;

  CVSearcher *searcher_;
  ConstraintPruner* pruner_;
	StateMerger* merger_;

	signal_ty pre_event_callbacks_;
	signal_ty post_event_callbacks_;

	std::vector<SocketEventList*> socket_events_;

	unsigned array_id_;
};


} // end namespace cliver

extern cliver::ClientVerifier *g_client_verifier;

#endif // CLIVER_H
