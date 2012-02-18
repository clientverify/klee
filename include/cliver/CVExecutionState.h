//===-- CVExecutionState.h --------------------------------------*- C++ -*-===//
//
// <insert license>
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//
#ifndef CLIVER_EXECUTION_STATE_H
#define CLIVER_EXECUTION_STATE_H

#include "cliver/ExecutionObserver.h"

#include "klee/ExecutionState.h"

#include "llvm/Instructions.h"

#include <list>
#include <sstream>
#include <ostream>

namespace klee {
struct KFunction;
class MemoryManager;
}

namespace cliver {
class AddressManager;
class CVContext;
class CVExecutionState;
class CVExecutor;
class ClientVerifier;
class ExecutionStateProperty;
class NetworkManager;
class PathManager;

////////////////////////////////////////////////////////////////////////////////

class CVExecutionState : public klee::ExecutionState, public ExecutionObserver {
 public:
  CVExecutionState(klee::KFunction *kF, klee::MemoryManager *mem);
  CVExecutionState(const std::vector< klee::ref<klee::Expr> > &assumptions);
  ~CVExecutionState();
  virtual CVExecutionState *branch();
  CVExecutionState *clone();

	int compare(const CVExecutionState& b) const;

	void get_pc_string(std::string &result, 
			llvm::Instruction* inst=NULL);

  void initialize(ClientVerifier *cv);
  int id() const { return id_; }
  const CVContext* context() { return context_; }

	NetworkManager* network_manager() const { return network_manager_; }
	PathManager* path_manager() { return path_manager_; }
	ExecutionStateProperty* property() { return property_; }

	void reset_path_manager(PathManager* path_manager=NULL);

#ifdef DEBUG_CLIVER_STATE_LOG
	std::stringstream& debug_log() { return *debug_log_; }
	void reset_debug_log() { delete debug_log_; debug_log_ = new std::stringstream(); }
	std::stringstream *debug_log_;
#endif

  void notify(ExecutionEvent ev) {}

  void print(std::ostream &os) const;

  ClientVerifier* cv() { return cv_; }

  bool basic_block_tracking() { return basic_block_tracking_; }
  void set_basic_block_tracking(bool b) { basic_block_tracking_ = b; }

 private:
  int increment_id() { return next_id_++; }

  int id_;
  static int next_id_;
  CVContext* context_;
	NetworkManager* network_manager_;
	PathManager* path_manager_;
	ExecutionStateProperty* property_;
  ClientVerifier *cv_;
  bool basic_block_tracking_;
};

////////////////////////////////////////////////////////////////////////////////

std::ostream &operator<<(std::ostream &os, const CVExecutionState &s);

////////////////////////////////////////////////////////////////////////////////

// is this crazy? yes i think so
struct CVExecutionStateDeleter {
	void operator()(CVExecutionState* state);
};

////////////////////////////////////////////////////////////////////////////////

typedef std::set<CVExecutionState*> ExecutionStateSet;

struct CVExecutionStateLT {
	bool operator()(const CVExecutionState* a, const CVExecutionState* b) const;
};

////////////////////////////////////////////////////////////////////////////////

} // End cliver namespace


#endif // CLIVER_EXECUTION_STATE_H
