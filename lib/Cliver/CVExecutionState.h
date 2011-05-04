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
#include "ClientVerifier.h"
#include "CVMemoryManager.h"

namespace klee {
  class KFunction;
}

namespace cliver {


class CVExecutionState : public klee::ExecutionState {
 public:
  CVExecutionState(klee::KFunction *kF, klee::MemoryManager *mem);
  //CVExecutionState(const std::vector< klee::ref<klee::Expr> > &assumptions);
  virtual ~CVExecutionState();
  virtual CVExecutionState *branch();

  int id() { return id_; }
  const CVContext* context() { return context_; }

  AddressManager* address_manager() { return address_manager_; }

 private:
  void initialize();
  int increment_id() { return next_id_++; }

  int id_;
  static int next_id_;
  CVContext* context_;
  AddressManager* address_manager_;
  CVMemoryManager* memory_;

};

} // End cliver namespace

#endif
