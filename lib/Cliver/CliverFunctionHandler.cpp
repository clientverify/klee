//===-- CliverFunctionHandler.cpp -------------------------------*- C++ -*-===//
//
// <insert license>
//
//===----------------------------------------------------------------------===//
//
// This file defines the CliverFunctionHandler class. 
//
//===----------------------------------------------------------------------===//




#include "CliverFunctionHandler.h"
#include "../Core/Common.h"
#include "../Core/Executor.h"
#include "../Core/Memory.h"
#include "../Core/MemoryManager.h"
#include "../Core/TimingSolver.h"
#include <errno.h>
#include "klee/ExecutionState.h"
#include "klee/Internal/Module/KInstruction.h"
#include "klee/Internal/Module/KModule.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Module.h"

using namespace llvm;
using namespace klee;

struct HandlerInfo {
  const char *name;
  CliverFunctionHandler::Handler handler;
  bool doesNotReturn; /// Intrinsic terminates the process
  bool hasReturnValue; /// Intrinsic has a return value
  bool doNotOverride; /// Intrinsic should not be used if already defined
};

HandlerInfo handlerInfo[] = {
#define add(name, handler, ret) { name, \
                                  &CliverFunctionHandler::handler, \
                                  false, ret, false }
#define addDNR(name, handler) { name, \
                                &CliverFunctionHandler::handler, \
                                true, false, false }

  add("placeholder", handlePlaceHolder, false),
#undef addDNR
#undef add  
};

CliverFunctionHandler::CliverFunctionHandler(Executor &_executor) 
  : executor(_executor) {}

void CliverFunctionHandler::prepare() {
  unsigned N = sizeof(handlerInfo)/sizeof(handlerInfo[0]);

  for (unsigned i=0; i<N; ++i) {
    HandlerInfo &hi = handlerInfo[i];
    Function *f = executor.kmodule->module->getFunction(hi.name);
    
    // No need to create if the function doesn't exist, since it cannot
    // be called in that case.
  
    if (f && (!hi.doNotOverride || f->isDeclaration())) {
      // Make sure NoReturn attribute is set, for optimization and
      // coverage counting.
      if (hi.doesNotReturn)
        f->addFnAttr(Attribute::NoReturn);

      // Change to a declaration since we handle internally (simplifies
      // module and allows deleting dead code).
      if (!f->isDeclaration())
        f->deleteBody();
    }
  }
}

void CliverFunctionHandler::bind() {
  unsigned N = sizeof(handlerInfo)/sizeof(handlerInfo[0]);

  for (unsigned i=0; i<N; ++i) {
    HandlerInfo &hi = handlerInfo[i];
    Function *f = executor.kmodule->module->getFunction(hi.name);
    
    if (f && (!hi.doNotOverride || f->isDeclaration()))
      handlers[f] = std::make_pair(hi.handler, hi.hasReturnValue);
  }
}


bool CliverFunctionHandler::handle(ExecutionState &state, 
                                    Function *f,
                                    KInstruction *target,
                                    std::vector< ref<Expr> > &arguments) {
  handlers_ty::iterator it = handlers.find(f);
  if (it != handlers.end()) {    
    Handler h = it->second.first;
    bool hasReturnValue = it->second.second;
     // FIXME: Check this... add test?
    if (!hasReturnValue && !target->inst->use_empty()) {
      executor.terminateStateOnExecError(state, 
                                         "expected return value from void special function");
    } else {
      (this->*h)(state, target, arguments);
    }
    return true;
  } else {
    return false;
  }
}

// reads a concrete string from memory
std::string 
CliverFunctionHandler::readStringAtAddress(ExecutionState &state, 
                                            ref<Expr> addressExpr) {
  ObjectPair op;
  addressExpr = executor.toUnique(state, addressExpr);
  ref<ConstantExpr> address = cast<ConstantExpr>(addressExpr);
  if (!state.addressSpace.resolveOne(address, op))
    assert(0 && "XXX out of bounds / multiple resolution unhandled");
  bool res;
  assert(executor.solver->mustBeTrue(state, 
                                     EqExpr::create(address, 
                                                    op.first->getBaseExpr()),
                                     res) &&
         res &&
         "XXX interior pointer unhandled");
  const MemoryObject *mo = op.first;
  const ObjectState *os = op.second;

  char *buf = new char[mo->size];

  unsigned i;
  for (i = 0; i < mo->size - 1; i++) {
    ref<Expr> cur = os->read8(i);
    cur = executor.toUnique(state, cur);
    assert(isa<ConstantExpr>(cur) && 
           "hit symbolic char while reading concrete string");
    buf[i] = cast<ConstantExpr>(cur)->getZExtValue(8);
  }
  buf[i] = 0;
  
  std::string result(buf);
  delete[] buf;
  return result;
}

