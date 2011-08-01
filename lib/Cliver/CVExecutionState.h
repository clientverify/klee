//===-- CVExecutionState.h --------------------------------------*- C++ -*-===//
//
// <insert license>
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//
#ifndef CLIVER_EXECUTIONSTATE_H
#define CLIVER_EXECUTIONSTATE_H

#include "klee/ExecutionState.h"

namespace klee {
class KFunction;
}

namespace cliver {
class AddressManager;
class CVContext;
class CVExecutor;
class NetworkManager;
class CVExecutionState;

class ExecutionStateInfo {
 public:
	ExecutionStateInfo(CVExecutionState* state);
	virtual void update(CVExecutionState* state);
	virtual bool less_than(const ExecutionStateInfo &info) const;
	virtual bool equals(const ExecutionStateInfo &info) const;
 private:
	int socket_log_index_;
};

struct ExecutionStateInfoLT {
	bool operator()(const ExecutionStateInfo &a, const ExecutionStateInfo &b) const;
};

typedef std::set<CVExecutionState*> ExecutionStateSet;

typedef std::map<ExecutionStateInfo, 
								 ExecutionStateSet,
								 ExecutionStateInfoLT> ExecutionStateMap;

class CVExecutionState : public klee::ExecutionState {
 public:
  CVExecutionState(klee::KFunction *kF);
  CVExecutionState(const std::vector< klee::ref<klee::Expr> > &assumptions);
  virtual ~CVExecutionState();
  virtual CVExecutionState *branch();

  void initialize(CVExecutor* executor);
  int id() { return id_; }
  const CVContext* context() { return context_; }

  AddressManager* address_manager() { return address_manager_; }
	NetworkManager* network_manager() { return network_manager_; }
	ExecutionStateInfo* info()			  { return info_; }

 private:
  int increment_id() { return next_id_++; }

  int id_;
  static int next_id_;
  CVContext* context_;
  AddressManager* address_manager_;
	NetworkManager* network_manager_;
	ExecutionStateInfo *info_;
};
} // End cliver namespace

#endif
