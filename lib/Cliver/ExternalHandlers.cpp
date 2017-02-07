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
#include "cliver/LazyConstraint.h"
#include "cliver/LazyConstraintConfig.h"
#include "CVCommon.h"

#include "../Core/Executor.h"
#include "../Core/Memory.h"
#include "../Core/TimingSolver.h"
#include "../Core/SpecialFunctionHandler.h"
#include "klee/Constants.h"
#include "klee/util/ExprUtil.h"
#include "klee/Internal/Module/KInstruction.h"
#include "klee/Interpreter.h"

#include "llvm/Support/CommandLine.h"

namespace cliver {

////////////////////////////////////////////////////////////////////////////////

llvm::cl::opt<bool>
XEventOptimization("xevent-optimization", llvm::cl::init(false));

llvm::cl::opt<unsigned>
QUEUE_SIZE("queue-size", llvm::cl::init(5));

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

  //uint64_t raw_address = cast<klee::ConstantExpr>(arguments[1])->getZExtValue();
  //klee::ObjectPair res;
  //static_cast<CVExecutor*>(executor)->resolve_one(state, address, res, true);
  //unsigned object_offset = raw_address - res.first->address;
  //CVMESSAGE("ExternalHandler_socket_read: offset: " << object_offset << ", " << raw_address << ", " << res.first->address);
  //klee::ObjectState *object = const_cast<klee::ObjectState*>(res.second);

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

  //uint64_t raw_address = cast<klee::ConstantExpr>(arguments[1])->getZExtValue();
  //klee::ObjectPair res;
  //static_cast<CVExecutor*>(executor)->resolve_one(state, address, res, true);
  //unsigned object_offset = raw_address - res.first->address;
  //CVMESSAGE("ExternalHandler_socket_write: offset: " << object_offset << ", " << raw_address << ", " << res.first->address);
  //klee::ObjectState *object = const_cast<klee::ObjectState*>(res.second);

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
  //CVExecutor *cv_executor = static_cast<CVExecutor*>(executor);
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
    CVMESSAGE("QUEUE set to " << QUEUE_SIZE);
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

void ExternalHandler_select(
    klee::Executor* executor, klee::ExecutionState *state,
    klee::KInstruction *target, std::vector<klee::ref<klee::Expr> > &arguments) {
  //CVExecutionState* cv_state = static_cast<CVExecutionState*>(state);
  //CVExecutor *cv_executor = static_cast<CVExecutor*>(executor);
}

void ExternalHandler_ktest_copy(
    klee::Executor* executor, klee::ExecutionState *state,
    klee::KInstruction *target, std::vector<klee::ref<klee::Expr> > &arguments) {

  CVExecutionState* cv_state = static_cast<CVExecutionState*>(state);
  CVExecutor *cv_executor = static_cast<CVExecutor*>(executor);
  std::string ktest_name = cv_executor->get_string_at_address(cv_state, arguments[0]);
  int ktest_index = cast<klee::ConstantExpr>(arguments[1])->getZExtValue();
  klee::ref<klee::Expr> address = arguments[2];
  uint64_t raw_address = cast<klee::ConstantExpr>(arguments[2])->getZExtValue();
  unsigned len = cast<klee::ConstantExpr>(arguments[3])->getZExtValue();
  //klee::ObjectState *object = resolve_address(executor, state, address, true);

  klee::ObjectPair res;
  static_cast<CVExecutor*>(executor)->resolve_one(state, address, res, true);
  unsigned object_offset = raw_address - res.first->address;

  cv_executor->ktest_copy(cv_state, target,
                          ktest_name, ktest_index,
                          const_cast<klee::ObjectState*>(res.second),
                          object_offset, len);
}

void ExternalHandler_tls_predict_stdin(
    klee::Executor* executor, klee::ExecutionState *state,
    klee::KInstruction *target, std::vector<klee::ref<klee::Expr> > &arguments) {

  CVExecutionState* cv_state = static_cast<CVExecutionState*>(state);
  CVExecutor *cv_executor = static_cast<CVExecutor*>(executor);
  size_t count = cast<klee::ConstantExpr>(arguments[0])->getZExtValue();

  cv_executor->tls_predict_stdin_size(cv_state, target, count);
}

void ExternalHandler_tls_master_secret(
    klee::Executor *executor, klee::ExecutionState *state,
    klee::KInstruction *target, std::vector<klee::ref<klee::Expr> > &arguments)
{

  CVExecutionState* cv_state = static_cast<CVExecutionState*>(state);
  CVExecutor* cv_executor = static_cast<CVExecutor*>(executor);
  klee::ref<klee::Expr> address = arguments[0];
  uint64_t raw_address = cast<klee::ConstantExpr>(arguments[0])->getZExtValue();

  klee::ObjectPair result;
  static_cast<CVExecutor*>(executor)->resolve_one(state, address, result, true);
  unsigned object_offset = raw_address - result.first->address;

  cv_executor->tls_master_secret(cv_state, target,
                                 const_cast<klee::ObjectState *>(result.second),
                                 object_offset);
}

void ExternalHandler_cliver_event(
    klee::Executor* executor, klee::ExecutionState *state,
    klee::KInstruction *target, std::vector<klee::ref<klee::Expr> > &arguments) {
  // cliver_event(EVENT_TYPE, PARAM_1<opt>, PARAM_2<opt>)
  
  int type = cast<klee::ConstantExpr>(arguments[0])->getZExtValue();
  CVExecutionState* cv_state = static_cast<CVExecutionState*>(state);
  CVExecutor *cv_executor = static_cast<CVExecutor*>(executor);

  if (type == KLEE_EVENT_SYMBOLIC_MODEL) {
    // Param1: address, Param2: bytecount
    klee::ref<klee::ConstantExpr> address = cast<klee::ConstantExpr>(arguments[1]);
    size_t length = cast<klee::ConstantExpr>(arguments[2])->getZExtValue();

    klee::ObjectPair op;

    if (!state->addressSpace.resolveOne(address, op))
      assert(0 && "XXX out of bounds / multiple resolution unhandled");
    bool res;
    assert(cv_executor->get_solver()->mustBeTrue(*state, 
                              klee::EqExpr::create(address, 
                                                   op.first->getBaseExpr()),
                                                   res) &&
          res &&
          "Symbolic Model Event: interior pointer unhandled");

    //const klee::MemoryObject *mo = op.first;
    const klee::ObjectState *os = op.second;

    std::vector<klee::ref<klee::Expr> > expr_bytes;
    std::vector<const klee::Array*> arrays;

    for (unsigned i=0; i<length; ++i) {
      expr_bytes.push_back(os->read8(i));
    }

    klee::findSymbolicObjects(expr_bytes.begin(), expr_bytes.end(), arrays);

    assert(arrays.size() && "SYMBOLIC EVENT: No arrays found!");

    for (unsigned i=0; i<arrays.size(); ++i) {
      CVMESSAGE("SYMBOLIC MODEL EVENT: " << arrays[i]->name << ", " << *cv_state);
    }

  }


}

/// Called from bitcode as follows:
///
///   cliver_lazy_constraint(uint8 *in_buf, size_t in_len,
///                          uint8 *out_buf, size_t out_len,
///                          const char *function_name)
///
/// TODO: we should create a cliver.h file like klee/klee.h
/// TODO: optional taint string
void ExternalHandler_lazy_constraint(
    klee::Executor *executor, klee::ExecutionState *state,
    klee::KInstruction *target, std::vector<klee::ref<klee::Expr>> &arguments) {
  using namespace klee;

  assert(arguments.size() == 5); // FIXME: optional taint string
  CVExecutionState *cv_state = static_cast<CVExecutionState *>(state);
  CVExecutor *cv_executor = static_cast<CVExecutor *>(executor);

  // Extract parameters (FIXME: we assume concrete params)
  ref<Expr> in_address_expr = arguments[0];
  uint64_t in_address = cast<ConstantExpr>(arguments[0])->getZExtValue();
  size_t in_len = cast<ConstantExpr>(arguments[1])->getZExtValue();
  ref<Expr> out_address_expr = arguments[2];
  uint64_t out_address = cast<ConstantExpr>(arguments[2])->getZExtValue();
  size_t out_len = cast<ConstantExpr>(arguments[3])->getZExtValue();
  std::string fname =
      cv_executor->get_string_at_address(cv_state, arguments[4]);

  // Compute memory object offsets
  ObjectPair in_result;
  cv_executor->resolve_one(state, in_address_expr, in_result, false);
  const MemoryObject *in_mo = in_result.first;
  const ObjectState *in_os = in_result.second;
  uint64_t in_offset = in_address - in_mo->address;

  ObjectPair out_result;
  cv_executor->resolve_one(state, out_address_expr, out_result, false);
  const MemoryObject *out_mo = out_result.first;
  const ObjectState *out_os = out_result.second;
  uint64_t out_offset = out_address - out_mo->address;

  // Create vectors of expressions representing in_buf and out_buf
  LazyConstraint::ExprVec inE;
  for (size_t i = 0; i < in_len; i++) {
    inE.push_back(in_os->read8(in_offset + i));
  }

  LazyConstraint::ExprVec outE;
  for (size_t i = 0; i < out_len; i++) {
    outE.push_back(out_os->read8(out_offset + i));
  }

  // Look up trigger function
  LazyTriggerFuncDB &ltfdb = LazyTriggerFuncDB::Instance();
  LazyConstraint::TriggerFunc f = ltfdb.find(fname);
  assert(f != NULL);

  // Create lazy constraint and insert into dispatcher
  std::shared_ptr<LazyConstraint> lazy_c =
      std::make_shared<LazyConstraint>(inE, outE, f, fname);
  LazyConstraintDispatcher &lcd = cv_state->get_lazy_constraint_dispatcher();
  lcd.addLazy(lazy_c);
}

////////////////////////////////////////////////////////////////////////////////

} // end namespace cliver
