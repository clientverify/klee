//===-- SpecialFunctionHandler-Cloud9.cpp ---------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "Common.h"

#include "Memory.h"
#include "SpecialFunctionHandler.h"
#include "TimingSolver.h"

#include "klee/ExecutionState.h"

#include "klee/Internal/Module/KInstruction.h"
#include "klee/Internal/Module/KModule.h"
#include "klee/util/ExprUtil.h"
#include "klee/Internal/Support/Debug.h"

#include "Executor.h"
#include "MemoryManager.h"

#if LLVM_VERSION_CODE >= LLVM_VERSION(3, 3)
#include "llvm/IR/Module.h"
#else
#include "llvm/Module.h"
#endif

#include <errno.h>
#include <iostream>

using namespace llvm;
using namespace klee;

// Cloud9 support

void SpecialFunctionHandler::handleEvent(ExecutionState &state,
                                         KInstruction *target,
                                         std::vector<ref<Expr> > &arguments) {
  assert(arguments.size() == 2 && "invalid number of arguments to klee_event");

  if (!isa<ConstantExpr>(arguments[0]) || !isa<ConstantExpr>(arguments[1])) {
    executor.terminateStateOnError(state, "klee_event requires a constant arg", "user.err");
    return;
  }

  ref<ConstantExpr> type = cast<ConstantExpr>(arguments[0]);
  ref<ConstantExpr> value = cast<ConstantExpr>(arguments[1]);

  executor.executeEvent(state,
                        (unsigned int)type->getZExtValue(),
                        (long int)value->getZExtValue());
}


void SpecialFunctionHandler::handleMakeShared(ExecutionState &state,
                          KInstruction *target,
                          std::vector<ref<Expr> > &arguments) {

  assert(arguments.size() == 2 &&
        "invalid number of arguments to klee_make_shared");
}

void SpecialFunctionHandler::handleGetWList(ExecutionState &state,
                    KInstruction *target,
                    std::vector<ref<Expr> > &arguments) {
  assert(arguments.empty() && "invalid number of arguments to klee_get_wlist");

  static uint64_t id = 0;

  executor.bindLocal(target, state, ConstantExpr::create(++id,
      executor.getWidthForLLVMType(target->inst->getType())));
}

void SpecialFunctionHandler::handleDebug(ExecutionState &state,
                                           KInstruction *target,
                                           std::vector<ref<Expr> > &arguments) {
  assert(arguments.size() >= 1 && "invalid number of arguments to klee_debug");

  std::string formatStr = readStringAtAddress(state, arguments[0]);

  if (arguments.size() == 2 && arguments[1]->getWidth() == sizeof(long)*8) {
    // Special case for displaying strings

    std::string paramStr = readStringAtAddress(state, arguments[1]);

    klee_message(formatStr.c_str(), paramStr.c_str());
    return;
  }

  std::vector<int> args;

  for (unsigned int i = 1; i < arguments.size(); i++) {
    if (!isa<ConstantExpr>(arguments[i])) {
      klee_message("%s: %s\n", formatStr.c_str(), "<nonconst args>");
      return;
    }

    ref<ConstantExpr> arg = cast<ConstantExpr>(arguments[i]);

    if (arg->getWidth() != sizeof(int)*8) {
      klee_message("%s: %s\n", formatStr.c_str(), "<non-32-bit args>");
      return;
    }

    args.push_back((int)arg->getZExtValue());
  }

  switch (args.size()) {
  case 0:
    klee_message("%s", formatStr.c_str());
    break;
  case 1:
    klee_message(formatStr.c_str(), args[0]);
    break;
  case 2:
    klee_message(formatStr.c_str(), args[0], args[1]);
    break;
  case 3:
    klee_message(formatStr.c_str(), args[0], args[1], args[2]);
    break;
  default:
    executor.terminateStateOnError(state,
                                   "klee_debug allows up to 3 arguments",
                                   "user.err");
    return;
  }
}
