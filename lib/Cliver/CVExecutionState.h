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

#include "ClientVerifier.h"
#include "klee/ExecutionState.h"
#include "llvm/Instructions.h"

#include <list>

namespace klee {
class KFunction;
class MemoryManager;
}

namespace cliver {
class AddressManager;
class CVContext;
class CVExecutionState;
class CVExecutor;
class ExecutionStateProperty;
class NetworkManager;
class PathManager;

////////////////////////////////////////////////////////////////////////////////

class CVExecutionState : public klee::ExecutionState {
 public:
  CVExecutionState(klee::KFunction *kF, klee::MemoryManager *mem);
  CVExecutionState(const std::vector< klee::ref<klee::Expr> > &assumptions);
  virtual ~CVExecutionState();
  virtual CVExecutionState *branch();
  CVExecutionState *clone();

	int compare(const CVExecutionState& b) const;

	void get_pc_string(std::string &result, 
			llvm::Instruction* inst=NULL);

  void initialize(CVExecutor* executor);
  int id() { return id_; }
  const CVContext* context() { return context_; }

	NetworkManager* network_manager() const { return network_manager_; }
	PathManager* path_manager() { return path_manager_; }
	ExecutionStateProperty* property() { return property_; }

	void reset_path_manager(PathManager* path_manager=NULL);

 private:
  int increment_id() { return next_id_++; }

  int id_;
  static int next_id_;
  CVContext* context_;
	NetworkManager* network_manager_;
	PathManager* path_manager_;
	ExecutionStateProperty* property_;
};

struct CVExecutionStateLT {
	bool operator()(const CVExecutionState* a, const CVExecutionState* b) const;
};

} // End cliver namespace

#endif // CLIVER_EXECUTION_STATE_H
