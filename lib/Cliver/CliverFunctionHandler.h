//===-- CliverFunctionHandler.h ---------------------------------*- C++ -*-===//
//
// <insert license>
//
//===----------------------------------------------------------------------===//
//
// CliverFunctionHandler is a clone of klee::SpecialFunctionHandler. The
// purpose of this class is to provide access to Cliver methods from within
// symbolically executing code. Function calls are intercepted during symbolic
// execution if the function name has declared handler here. 
//
//===----------------------------------------------------------------------===//

#ifndef CLIVERFUNCTIONHANDLER_H
#define CLIVERFUNCTIONHANDLER_H
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
  
  class CliverFunctionHandler {
  public:
    class Executor &executor;
    typedef void (CliverFunctionHandler::*Handler)(ExecutionState &state,
                                                   KInstruction *target, 
                                                   std::vector<ref<Expr> > 
                                                     &arguments);
    typedef std::map<const llvm::Function*, 
                     std::pair<Handler,bool> > handlers_ty;

    handlers_ty handlers;

  public:
    CliverFunctionHandler(Executor &_executor);

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
    HANDLER(handlePlaceHolder);
#undef HANDLER
  };
} // End klee namespace

#endif //CLIVERFUNCTIONHANDLER_H
