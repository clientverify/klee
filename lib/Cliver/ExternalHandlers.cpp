//===-- ExternalHandlers.cpp ------------------------------------*- C++ -*-===//
//
// <insert license>
//
//===----------------------------------------------------------------------===//
//
// TODO: Description of each handler
//
//===----------------------------------------------------------------------===//

#include "ExternalHandlers.h"

#include "cliver/ClientVerifier.h"
#include "cliver/CVExecutionState.h"
#include "cliver/CVExecutor.h"
#include "cliver/CVStream.h"
#include "cliver/ExecutionObserver.h"
#include "cliver/NetworkManager.h"
#include "CVCommon.h"

#include "../Core/Memory.h"
#include "klee/Executor.h"
#include "klee/Internal/Module/KInstruction.h"
#include "klee/Interpreter.h"

#include "llvm/Support/CommandLine.h"

namespace cliver {

////////////////////////////////////////////////////////////////////////////////

llvm::cl::opt<bool>
XEventOptimization("xevent-optimization", llvm::cl::init(false));

llvm::cl::opt<unsigned>
QUEUE_SIZE("queue-size", llvm::cl::init(3));

////////////////////////////////////////////////////////////////////////////////

klee::ObjectState* resolve_address(klee::Executor* executor, 
    klee::ExecutionState* state, klee::ref<klee::Expr> address,
    bool writeable=false) {
  klee::ObjectPair result;
  static_cast<CVExecutor*>(executor)->resolve_one(state, address, result, writeable);
  return const_cast<klee::ObjectState*>(result.second);
}

void ExternalHandler_socket_create(
    klee::Executor* executor, klee::ExecutionState *state, 
    klee::KInstruction *target, std::vector<klee::ref<klee::Expr> > &arguments) {

  int domain 	 = cast<klee::ConstantExpr>(arguments[0])->getZExtValue();
  int type     = cast<klee::ConstantExpr>(arguments[1])->getZExtValue();
  int protocol = cast<klee::ConstantExpr>(arguments[2])->getZExtValue();

  CVExecutionState* cv_state = static_cast<CVExecutionState*>(state);
  CVExecutor *cv_executor = static_cast<CVExecutor*>(executor);
  cv_state->network_manager()->execute_open_socket(cv_executor, target, 
      domain, type, protocol);
}

void ExternalHandler_socket_read(
    klee::Executor* executor, klee::ExecutionState *state, 
    klee::KInstruction *target, std::vector<klee::ref<klee::Expr> > &arguments) {
  assert(arguments.size() >= 3);

  int fd = cast<klee::ConstantExpr>(arguments[0])->getZExtValue();
  klee::ref<klee::Expr> address = arguments[1];
  int len = cast<klee::ConstantExpr>(arguments[2])->getZExtValue();
  klee::ObjectState *object = resolve_address(executor, state, address,
                                              true);

  CVExecutionState* cv_state = static_cast<CVExecutionState*>(state);
  CVExecutor *cv_executor = static_cast<CVExecutor*>(executor);
  cv_state->network_manager()->execute_read(cv_executor, target, object, fd, len);
}

void ExternalHandler_socket_write(
    klee::Executor* executor, klee::ExecutionState *state, 
    klee::KInstruction *target, std::vector<klee::ref<klee::Expr> > &arguments) {
  assert(arguments.size() >= 3);

  int fd = cast<klee::ConstantExpr>(arguments[0])->getZExtValue();
  klee::ref<klee::Expr> address = arguments[1];
  int len = cast<klee::ConstantExpr>(arguments[2])->getZExtValue();
  klee::ObjectState *object = resolve_address(executor, state, address);

  CVExecutionState* cv_state = static_cast<CVExecutionState*>(state);
  CVExecutor *cv_executor = static_cast<CVExecutor*>(executor);
  cv_state->network_manager()->execute_write(cv_executor, target, object, fd, len);
}

void ExternalHandler_socket_shutdown(
    klee::Executor* executor, klee::ExecutionState *state, 
    klee::KInstruction *target, std::vector<klee::ref<klee::Expr> > &arguments) {
  assert(arguments.size() >= 2);

  int fd  = cast<klee::ConstantExpr>(arguments[0])->getZExtValue();
  int how = cast<klee::ConstantExpr>(arguments[1])->getZExtValue();

  CVExecutionState* cv_state = static_cast<CVExecutionState*>(state);
  CVExecutor *cv_executor = static_cast<CVExecutor*>(executor);
  cv_state->network_manager()->execute_shutdown(cv_executor, target, fd, how);
}

void ExternalHandler_merge(
    klee::Executor* executor, klee::ExecutionState *state, 
    klee::KInstruction *target, std::vector<klee::ref<klee::Expr> > &arguments) {
  assert(arguments.size() == 0);
  CVExecutor *cv_executor = static_cast<CVExecutor*>(executor);
}

void ExternalHandler_XEventsQueued(
    klee::Executor* executor, klee::ExecutionState *state, 
    klee::KInstruction *target, std::vector<klee::ref<klee::Expr> > &arguments) {
  assert(arguments.size() == 0);
  CVExecutor *cv_executor = static_cast<CVExecutor*>(executor);
  CVExecutionState* cv_state = static_cast<CVExecutionState*>(state);

  if (XEventOptimization
      && cv_state->network_manager()->socket()->type() != SocketEvent::SEND) {
      cv_executor->bind_local(target, cv_state, 0);
  } else {
    cv_executor->bind_local(target, cv_state, QUEUE_SIZE);
  }
}

void ExternalHandler_CliverPrint(
    klee::Executor* executor, klee::ExecutionState *state, 
    klee::KInstruction *target, std::vector<klee::ref<klee::Expr> > &arguments) {
  assert(arguments.size() >= 1);
  if (arguments.size() > 1) {
    CVMESSAGE("cliver_print called with more than one arg (not supported)");
  }
  CVExecutor *cv_executor = static_cast<CVExecutor*>(executor);
  CVExecutionState* cv_state = static_cast<CVExecutionState*>(state);
  *cv_message_stream 
      << cv_state->cv()->client_name()
      << " [" << cv_state->id() << "] "
      << cv_executor->get_string_at_address(cv_state, arguments[0])
      << "\n";
}

void ExternalHandler_EnableBasicBlockTracking(
    klee::Executor* executor, klee::ExecutionState *state, 
    klee::KInstruction *target, std::vector<klee::ref<klee::Expr> > &arguments) {
  assert(arguments.size() == 0);
  CVExecutionState* cv_state = static_cast<CVExecutionState*>(state);
  cv_state->set_basic_block_tracking(true);
}

void ExternalHandler_DisableBasicBlockTracking(
    klee::Executor* executor, klee::ExecutionState *state, 
    klee::KInstruction *target, std::vector<klee::ref<klee::Expr> > &arguments) {
  assert(arguments.size() == 0);
  CVExecutionState* cv_state = static_cast<CVExecutionState*>(state);
  cv_state->set_basic_block_tracking(false);
}

void ExternalHandler_Finish(
    klee::Executor* executor, klee::ExecutionState *state, 
    klee::KInstruction *target, std::vector<klee::ref<klee::Expr> > &arguments) {
  assert(arguments.size() == 0);
  //CVExecutionState* cv_state = static_cast<CVExecutionState*>(state);
  //CVExecutor *cv_executor = static_cast<CVExecutor*>(executor);
}

void ExternalHandler_select_event(
    klee::Executor* executor, klee::ExecutionState *state, 
    klee::KInstruction *target, std::vector<klee::ref<klee::Expr> > &arguments) {
  assert(arguments.size() == 0);
  //CVExecutionState* cv_state = static_cast<CVExecutionState*>(state);
  //CVExecutor *cv_executor = static_cast<CVExecutor*>(executor);
}


////////////////////////////////////////////////////////////////////////////////

} // end namespace cliver
