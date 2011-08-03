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
class MemoryManager;
}

namespace cliver {
class AddressManager;
class CVContext;
class CVExecutionState;
class CVExecutor;
class NetworkManager;
class PathManager;

class ExecutionStateInfo {
 public:
	ExecutionStateInfo(CVExecutionState* state);
	virtual void update(CVExecutionState* state);
	virtual bool less_than(const ExecutionStateInfo &info) const;
	virtual bool equals(const ExecutionStateInfo &info) const;
  virtual void print(std::ostream &os) const;
 private:
	int socket_log_index_;
};

inline std::ostream &operator<<(std::ostream &os, const ExecutionStateInfo &info) {
  info.print(os);
  return os;
}
 
struct ExecutionStateInfoLT {
	bool operator()(const ExecutionStateInfo &a, const ExecutionStateInfo &b) const;
};

typedef std::set<CVExecutionState*> ExecutionStateSet;

typedef std::map<ExecutionStateInfo, 
								 ExecutionStateSet,
								 ExecutionStateInfoLT> ExecutionStateMap;

class CVExecutionState : public klee::ExecutionState {
 public:
  CVExecutionState(klee::KFunction *kF, klee::MemoryManager *mem);
  CVExecutionState(const std::vector< klee::ref<klee::Expr> > &assumptions);
  virtual ~CVExecutionState();
  virtual CVExecutionState *branch();

  void initialize(CVExecutor* executor);
  int id() { return id_; }
  const CVContext* context() { return context_; }

  AddressManager* address_manager() { return address_manager_; }
	NetworkManager* network_manager() { return network_manager_; }
	PathManager*    path_manager() { return path_manager_; }
	ExecutionStateInfo* info()			  { return info_; }

	uint64_t get_symbolic_name_id(std::string &name);

 private:
  int increment_id() { return next_id_++; }

  int id_;
  static int next_id_;
  CVContext* context_;
  AddressManager* address_manager_;
	NetworkManager* network_manager_;
	PathManager* path_manager_;
	ExecutionStateInfo* info_;
	std::map< std::string, uint64_t > symbolic_name_map_;
};
} // End cliver namespace

#endif
