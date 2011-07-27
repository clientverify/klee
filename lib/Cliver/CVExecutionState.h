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
class CVMemoryManager;
class NetworkManager;

class CVExecutionState : public klee::ExecutionState {
 public:
  CVExecutionState(klee::KFunction *kF, klee::MemoryManager *mem);
  CVExecutionState(const std::vector< klee::ref<klee::Expr> > &assumptions);
  virtual ~CVExecutionState();
  virtual CVExecutionState *branch();

  int id() { return id_; }
  const CVContext* context() { return context_; }

  AddressManager* address_manager() { return address_manager_; }
	NetworkManager* network_manager() { return network_manager_; }

 private:
  void initialize();
  int increment_id() { return next_id_++; }

  int id_;
  static int next_id_;
  CVContext* context_;
  AddressManager* address_manager_;
  CVMemoryManager* memory_;
	NetworkManager* network_manager_;

};
} // End cliver namespace

#endif
