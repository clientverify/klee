/* NUKLEAR KLEE begin (ENTIRE FILE) */
//===-- NuklearFunctionHandler.h --------------------------------*- C++ -*-===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef KLEE_NUKLEARFUNCTIONHANDLER_H
#define KLEE_NUKLEARFUNCTIONHANDLER_H

#include <map>
#include <vector>
#include <string>

namespace llvm {
  class Function;
}

namespace klee {
  class Executor;
  class Expr;
  class ExecutionState;
  class KInstruction;
  template<typename T> class ref;
  
  class NuklearFunctionHandler {
  public:
    class Executor &executor;
    class NuklearManager &nuklearManager;
    ref<Expr> constraints;
    std::vector<ref<Expr> > constraint_vec;
    typedef void (NuklearFunctionHandler::*Handler)(ExecutionState &state,
                                                    KInstruction *target, 
                                                    std::vector<ref<Expr> > 
                                                      &arguments);
    typedef std::map<const llvm::Function*, 
                     std::pair<Handler,bool> > handlers_ty;

    handlers_ty handlers;

  public:
    NuklearFunctionHandler(Executor &_executor, NuklearManager &_nuklearManager);

    /// Perform any modifications on the LLVM module before it is
    /// prepared for execution. At the moment this involves deleting
    /// unused function bodies and marking intrinsics with appropriate
    /// flags for use in optimizations.
    void prepare();

    /// Initialize the internal handler map after the module has been
    /// prepared for execution.
    void bind();

    bool handle(ExecutionState &state, 
                llvm::Function *f,
                KInstruction *target,
                std::vector< ref<Expr> > &arguments);

    /* Convenience routines */

    std::string readStringAtAddress(ExecutionState &state, ref<Expr> address);
    
    /* Handlers */

#define HANDLER(name) void name(ExecutionState &state, \
                                KInstruction *target, \
                                std::vector< ref<Expr> > &arguments)
    HANDLER(handleGetExecutionStateID);
    HANDLER(handleDuplicateSymbolic);
    HANDLER(handleMakeSymbolicUnknownSize);
    HANDLER(handleCopyAndMakeSymbolic);
    HANDLER(handleAddExternalObject);
    HANDLER(handleAddEqExprToConstraints);
    HANDLER(handleWriteAllConstraints);
    HANDLER(handleDisableExternals);
    HANDLER(handleDisableXWindows);
    HANDLER(handleReplaySocketRead);
    HANDLER(handleReplaySocketWrite);
    HANDLER(handlePrintState);
    HANDLER(handleNuklearMakeSymbolic);
    HANDLER(handleXEventsQueued);
#undef HANDLER
  };
} // End klee namespace

#endif
/* NUKLEAR KLEE end (ENTIRE FILE) */
