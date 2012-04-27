//===-- ExternalHandlers.h --------------------------------------*- C++ -*-===//
//
// <insert license>
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//
#ifndef CLIVER_EXTERNAL_HANDLERS_H
#define CLIVER_EXTERNAL_HANDLERS_H

#include "klee/Expr.h"

#include <vector>

namespace klee {
  class KInstruction;
  class ExecutionState;
  class Executor;
}

namespace cliver {

////////////////////////////////////////////////////////////////////////////////

void ExternalHandler_socket_create(
  klee::Executor* executor, klee::ExecutionState *state, 
  klee::KInstruction *target, std::vector<klee::ref<klee::Expr> > &arguments);

void ExternalHandler_socket_read(
  klee::Executor* executor, klee::ExecutionState *state, 
  klee::KInstruction *target, std::vector<klee::ref<klee::Expr> > &arguments);

void ExternalHandler_socket_write(
  klee::Executor* executor, klee::ExecutionState *state, 
  klee::KInstruction *target, std::vector<klee::ref<klee::Expr> > &arguments);

void ExternalHandler_socket_shutdown(
  klee::Executor* executor, klee::ExecutionState *state, 
  klee::KInstruction *target, std::vector<klee::ref<klee::Expr> > &arguments);

void ExternalHandler_merge(
    klee::Executor* executor, klee::ExecutionState *state, 
    klee::KInstruction *target, std::vector<klee::ref<klee::Expr> > &arguments);

void ExternalHandler_XEventsQueued(
    klee::Executor* executor, klee::ExecutionState *state, 
    klee::KInstruction *target, std::vector<klee::ref<klee::Expr> > &arguments);

void ExternalHandler_CliverPrint(
    klee::Executor* executor, klee::ExecutionState *state, 
    klee::KInstruction *target, std::vector<klee::ref<klee::Expr> > &arguments);

void ExternalHandler_EnableBasicBlockTracking(
    klee::Executor* executor, klee::ExecutionState *state, 
    klee::KInstruction *target, std::vector<klee::ref<klee::Expr> > &arguments);

void ExternalHandler_DisableBasicBlockTracking(
    klee::Executor* executor, klee::ExecutionState *state, 
    klee::KInstruction *target, std::vector<klee::ref<klee::Expr> > &arguments);

void ExternalHandler_Finish(
    klee::Executor* executor, klee::ExecutionState *state, 
    klee::KInstruction *target, std::vector<klee::ref<klee::Expr> > &arguments);

////////////////////////////////////////////////////////////////////////////////

} // end namespace cliver

#endif //CLIVER_EXTERNAL_HANDLERS_H
