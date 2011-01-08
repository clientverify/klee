/* NUKLEAR KLEE begin(ENTIRE FILE) */
//===-- NuklearFunctionHandler.cpp ----------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "Common.h"

#include "Memory.h"
#include "NuklearFunctionHandler.h"
#include "TimingSolver.h"

#include "klee/ExecutionState.h"
#include "NuklearManager.h"

#include "klee/util/ExprPPrinter.h"
#include "klee/Internal/Module/KInstruction.h"
#include "klee/Internal/Module/KModule.h"

#include "Executor.h"
#include "MemoryManager.h"

#include "llvm/Module.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Support/raw_ostream.h"

#include <errno.h>

using namespace llvm;
using namespace klee;

#include <cassert>
#include <algorithm>
#include <iostream>
#include <iomanip>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>


extern bool NoExternals;
extern bool NoXWindows;

/// \todo Almost all of the demands in this file should be replaced
/// with terminateState calls.
///

struct HandlerInfo {
  const char *name;
  NuklearFunctionHandler::Handler handler;
  bool doesNotReturn; /// Intrinsic terminates the process
  bool hasReturnValue; /// Intrinsic has a return value
  bool doNotOverride; /// Intrinsic should not be used if already defined
};

// FIXME: We are more or less committed to requiring an intrinsic
// library these days. We can move some of this stuff there,
// especially things like realloc which have complicated semantics
// w.r.t. forking. Among other things this makes delayed query
// dispatch easier to implement.

HandlerInfo nuklearHandlerInfo[] = {
#define add(name, handler, ret) { name, \
                                  &NuklearFunctionHandler::handler, \
                                  false, ret, false }
#define addDNR(name, handler) { name, \
                                &NuklearFunctionHandler::handler, \
                                true, false, false }
  add("klee_get_executionstate_id", handleGetExecutionStateID, true), 
  add("klee_duplicate_symbolic", handleDuplicateSymbolic, true), 
  add("klee_make_symbolic_unknown_size", handleMakeSymbolicUnknownSize, false),
  add("klee_copy_and_make_symbolic", handleCopyAndMakeSymbolic, false),
  add("klee_add_external_object", handleAddExternalObject, false),
  add("klee_equal", handleAddEqExprToConstraints, false),
  add("klee_write_constraints", handleWriteAllConstraints, false), 
  add("klee_disable_externals", handleDisableExternals, false), 
  add("klee_disable_xwindows", handleDisableXWindows, false), 
  add("klee_socket_write", handleReplaySocketWrite, true), 
  add("klee_socket_read", handleReplaySocketRead, true), 
  add("klee_print_state", handlePrintState, false), 
  add("klee_nuklear_make_symbolic", handleNuklearMakeSymbolic, false), 
  add("klee_nuklear_XEventsQueued", handleXEventsQueued, true), 
#undef addDNR
#undef add  
};

NuklearFunctionHandler::NuklearFunctionHandler(Executor &_executor,
                                               NuklearManager &_nuklearManager)
  : executor(_executor), nuklearManager(_nuklearManager),
    constraints(ConstantExpr::alloc(true, Expr::Bool)){}


void NuklearFunctionHandler::prepare() {
  unsigned N = sizeof(nuklearHandlerInfo)/sizeof(nuklearHandlerInfo[0]);

  for (unsigned i=0; i<N; ++i) {
    HandlerInfo &hi = nuklearHandlerInfo[i];
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

void NuklearFunctionHandler::bind() {
  unsigned N = sizeof(nuklearHandlerInfo)/sizeof(nuklearHandlerInfo[0]);

  for (unsigned i=0; i<N; ++i) {
    HandlerInfo &hi = nuklearHandlerInfo[i];
    Function *f = executor.kmodule->module->getFunction(hi.name);
    
    if (f && (!hi.doNotOverride || f->isDeclaration()))
      handlers[f] = std::make_pair(hi.handler, hi.hasReturnValue);
  }
}


bool NuklearFunctionHandler::handle(ExecutionState &state, 
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

/****/

// reads a concrete string from memory
std::string 
NuklearFunctionHandler::readStringAtAddress(ExecutionState &state, 
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

/****/

void NuklearFunctionHandler::handleGetExecutionStateID(ExecutionState &state,
                                                       KInstruction *target,
                                                       std::vector<ref<Expr> > &arguments) {
  assert(arguments.size()==0 &&
         "invalid number of arguments to klee_get_executionstate_id");
  
  executor.bindLocal(target, state,
                     ConstantExpr::create(state.id, Expr::Int32));
}

void NuklearFunctionHandler::handleAddExternalObject(ExecutionState &state,
                                                     KInstruction *target,
                                                     std::vector<ref<Expr> > &arguments) {
  assert(arguments.size()==2 &&
         "invalid number of arguments to klee_add_external_object");
  assert(isa<ConstantExpr>(arguments[0]) &&
         "expect constant address argument to klee_add_external_object");
  assert(isa<ConstantExpr>(arguments[1]) &&
         "expect constant size argument to klee_add_external_object");
  
  uint64_t address = cast<ConstantExpr>(arguments[0])->getZExtValue();
  uint64_t size = cast<ConstantExpr>(arguments[1])->getZExtValue();
  executor.addExternalObject(state, (void*) address, (unsigned)size, false);
}

// (rac) Still used? I think we only needed this for eager 
void NuklearFunctionHandler::handleAddEqExprToConstraints(ExecutionState &state,
                                                          KInstruction *target,
                                                          std::vector<ref<Expr> > &arguments) {
  assert(arguments.size()==2 &&
         "invalid number of arguments to klee_add_equals");

  Executor::ExactResolutionList rl;
  executor.resolveExact(state, arguments[0], rl, "add_equals");
   
  const MemoryObject *mo = NULL;
  const ObjectState *os = NULL;
  ExecutionState *s;

  // we should only enter this block once.
  for (Executor::ExactResolutionList::iterator it = rl.begin(), 
         ie = rl.end(); it != ie; ++it) {
    mo = it->first.first;
    os = it->first.second;
    s = it->second;
    //klee_warning("add_equals: original resolved: %s, %d bytes", 
    //             mo->name.c_str(), mo->size);
  }

  Executor::ExactResolutionList rl_new;
  executor.resolveExact(state, arguments[1], rl_new, "add_equals");

  const MemoryObject *mo_new = NULL;
  const ObjectState *os_new;
  ExecutionState *s_new;

  // we should only enter this block once.
  for (Executor::ExactResolutionList::iterator it = rl_new.begin(), 
         ie = rl_new.end(); it != ie; ++it) {
    mo_new = it->first.first;
    os_new = it->first.second;
    s_new = it->second;
    //klee_warning("add_equals: copy resolved: %s, %d bytes", 
    //             mo_new->name.c_str(), mo_new->size);
  }
  assert( mo && mo_new );

  //klee_warning("add_equals( %d bytes == %d bytes )", mo->size, mo_new->size);
  assert(mo->size == mo_new->size);

  // Split the constraints to 512 byte blocks for better performance, the
  // ExprVisitor class in Constraints.cpp crashes when the Expr > 100 KB.
  unsigned stripe = 512;
  while (stripe > 1 && mo->size % stripe != 0) stripe >>= 1;
  if (mo->size > 16 || mo->size % 2 != 0) {
    unsigned offset=0, width=0;
    unsigned i = 0;
    while ( i < mo->size) {
      offset = i;
      width  = ((i + stripe) < mo->size) ? stripe : (mo->size - i);
      //klee_warning("add_equals( %s[%d], %s[%d] ) == %d bytes", 
      //             mo->name.c_str(), offset, mo_new->name.c_str(), offset, width);
      
      ref<Expr> expr 
        = EqExpr::create(os->read(offset, width*8), os_new->read(offset, width*8));
      constraint_vec.push_back(expr);
      i += stripe;
    }
  } else {
    //klee_warning("add_equals( %s, %s ) == %d bytes", 
    //             mo->name.c_str(), mo_new->name.c_str(), mo->size);
    
    ref<Expr> expr 
      = EqExpr::create(os->read(0, mo->size*8), os_new->read(0, mo_new->size*8));
    constraint_vec.push_back(expr);
  }
  return;
}

// (rac) Still used? I think we only needed this for eager 
void NuklearFunctionHandler::handleDuplicateSymbolic(ExecutionState &state,
                                                     KInstruction *target,
                                                     std::vector<ref<Expr> > &arguments) {
  std::string name;

  //assert(arguments.size()==1 &&
  //       "invalid number of arguments to klee_duplicate_symbolic");  
  
  Executor::ExactResolutionList rl;
  executor.resolveExact(state, arguments[0], rl, "duplicate_symbolic");
   
  for (Executor::ExactResolutionList::iterator it = rl.begin(), 
         ie = rl.end(); it != ie; ++it) {
    MemoryObject *mo = (MemoryObject*) it->first.first;

    //const ObjectState *old = it->first.second;
    //ExecutionState *s = it->second;
    executor.executeAlloc(state, ConstantExpr::create(mo->size, 32), false, target);
    ref<Expr> address_new = executor.getDestCell(state, target).value;
    
    Executor::ExactResolutionList rl_new;
    executor.resolveExact(state, address_new, rl_new, "duplicate_symbolic_2");
 
    if (mo->name.substr(0,7) == std::string("unnamed"))
      name = readStringAtAddress(state, arguments[1]) + "__COPY__";
    else
      name = mo->name + "__COPY__";

    MemoryObject *mo_new;
    const ObjectState *old_new;
    ExecutionState *s_new;

    for (Executor::ExactResolutionList::iterator it_new = rl_new.begin(), 
           ie_new = rl_new.end(); it_new != ie_new; ++it_new) {
      mo_new = (MemoryObject*) it_new->first.first;
      mo_new->setName(name);
      //klee_warning("duplicate_symbolic: %s, %d bytes", name.c_str(), mo_new->size);
      old_new = it_new->first.second;
      s_new = it_new->second;
      executor.executeMakeSymbolic(*s_new, mo_new);
    }
  }
}

void NuklearFunctionHandler::handleWriteAllConstraints(ExecutionState &state,
                                                       KInstruction *target,
                                                       std::vector<ref<Expr> > &arguments) {
  assert(arguments.size()==0 &&
         "invalid number of arguments to klee_write_constraints");

  ConstraintManager constraint_manager(state.constraints.constraints);
  for(std::vector< ref<Expr> >::iterator it=constraint_vec.begin(),
                                         ie=constraint_vec.end();
      it != ie; it++) {
    constraint_manager.constraints.push_back(*it);
  }

  static unsigned id = 0; 
  char filename[128];
  sprintf(filename, "gsec_%06d.%s", ++id, "pc");
  std::ostringstream info;
  ExprPPrinter::printConstraints(info, constraint_manager);
  std::string constraints_str = info.str();    
  std::ostream *f = executor.interpreterHandler->openOutputFile(filename);
  *f << constraints_str;
  delete f;

  constraint_vec.clear();
}

void NuklearFunctionHandler::handleMakeSymbolicUnknownSize(ExecutionState &state,
                                                KInstruction *target,
                                                std::vector<ref<Expr> > &arguments) {
  std::string name;

  // FIXME: For backwards compatibility, we should eventually enforce the
  // correct arguments.
  if (arguments.size() == 1) {
    name = "unnamed";
  } else {
    // FIXME: Should be a user.err, not an assert.
    assert(arguments.size()==2 &&
           "invalid number of arguments to klee_make_symbolic");  
    name = readStringAtAddress(state, arguments[1]);
  }

  Executor::ExactResolutionList rl;
  executor.resolveExact(state, arguments[0], rl, "make_symbolic_unknown_size");
  
  for (Executor::ExactResolutionList::iterator it = rl.begin(), 
         ie = rl.end(); it != ie; ++it) {
    MemoryObject *mo = (MemoryObject*) it->first.first;
    mo->setName(name);
    
    ObjectState *old = const_cast<ObjectState*>(it->first.second);
    ExecutionState *s = it->second;


    if (old->readOnly) {
      executor.terminateStateOnError(*s, 
                                     "cannot make readonly object symbolic", 
                                     "user.err");
      return;
    } 
    
    executor.executeMakeSymbolic(*s, mo);

  }
}

void NuklearFunctionHandler::handleCopyAndMakeSymbolic(ExecutionState &state,
                                                KInstruction *target,
                                                std::vector<ref<Expr> > &arguments) {
  std::string name;

  // FIXME: For backwards compatibility, we should eventually enforce the
  // correct arguments.
  if (arguments.size() == 1) {
    name = "unnamed";
  } else {
    // FIXME: Should be a user.err, not an assert.
    assert(arguments.size()==2 &&
           "invalid number of arguments to klee_copy_and_make_symbolic");  
    name = readStringAtAddress(state, arguments[1]);
  }

  Executor::ExactResolutionList rl;
  executor.resolveExact(state, arguments[0], rl, "copy_and_make_symbolic");
  
  for (Executor::ExactResolutionList::iterator it = rl.begin(), 
         ie = rl.end(); it != ie; ++it) {
    MemoryObject *mo = (MemoryObject*) it->first.first;
    mo->setName(name);
    
    ObjectState *old = const_cast<ObjectState*>(it->first.second);
    ref<Expr> old_expr = old->read(0, mo->size*8);
    ExecutionState *s = it->second;

    if (old->readOnly) {
      executor.terminateStateOnError(*s, 
                                     "cannot make readonly object symbolic", 
                                     "user.err");
      return;
    } 
    
    executor.executeMakeSymbolic(*s, mo);

    Executor::ExactResolutionList rl_sym;
    executor.resolveExact(state, arguments[0], rl_sym, "copy_and_make_symbolic_2");
    ObjectState *os_new = const_cast<ObjectState*>(rl_sym.begin()->first.second);
    os_new->write(0, old_expr);
  }
}

void NuklearFunctionHandler::handleXEventsQueued(ExecutionState &state,
                                            KInstruction *target,
                                            std::vector<ref<Expr> > &arguments) {
  nuklearManager.handleXEventsQueued(state, target);
}

void NuklearFunctionHandler::handleDisableExternals(ExecutionState &state,
                                            KInstruction *target,
                                            std::vector<ref<Expr> > &arguments) {
  // XXX should type check args
  assert(arguments.size()==0 &&
         "invalid number of arguments to klee_disable_externals");
  NoExternals = true;
}

void NuklearFunctionHandler::handleDisableXWindows(ExecutionState &state,
                                            KInstruction *target,
                                            std::vector<ref<Expr> > &arguments) {
  // XXX should type check args
  assert(arguments.size()==0 &&
         "invalid number of arguments to klee_disable_externals");
  NoXWindows = true;
}

void NuklearFunctionHandler::handleReplaySocketRead(ExecutionState &state,
                                            KInstruction *target,
                                            std::vector<ref<Expr> > &arguments) {

  // int klee_socket_read(int socket_id, void* address, size_t size)

  assert(arguments.size()==3 &&
         "invalid number of arguments to klee_socket_read");  

  nuklearManager.executeSocketRead(state, target,
                                   arguments[0], arguments[1], arguments[2]);
 }

void NuklearFunctionHandler::handleReplaySocketWrite(ExecutionState &state,
                                            KInstruction *target,
                                            std::vector<ref<Expr> > &arguments) {

  // int klee_socket_write(int socket_id, void* address, size_t size)

  assert(arguments.size()==3 &&
         "invalid number of arguments to klee_socket_write");

  nuklearManager.executeSocketWrite(state, target,
                                    arguments[0], arguments[1], arguments[2]);
}

void NuklearFunctionHandler::handlePrintState(ExecutionState &state,
                                            KInstruction *target,
                                            std::vector<ref<Expr> > &arguments) {
  llvm::errs() << "-- state " << state.id << " " << &state<< " --\n";
  if (state.pc)
    llvm::errs() << "-- pc: \n" << *state.pc->inst << "\n";
 
  llvm::errs() << "-- symbolics: [";
  for(typeof(state.symbolics.begin()) it=state.symbolics.begin(), 
                                      ie=state.symbolics.end(); it!=ie; ++it) {
    llvm::errs() << it->first->name << ", ";
  }

  llvm::errs() << "]\n";


  llvm::errs() << "-- stackframe:\n";
  std::vector<StackFrame>::const_iterator stack_it = state.stack.begin();
  while (stack_it != state.stack.end()) {
    if (KFunction *kf = stack_it->kf) {
      if (llvm::Function *function = kf->function) {
        llvm::errs() << "  -- function: " << function->getNameStr() << "\n";
      }
    }

    if (KInstruction *ki = stack_it->caller) {
      if (llvm::Instruction *inst = ki->inst) {
        llvm::errs() << "  -- inst: " << inst->getNameStr() << "\n";
      }
    }
    ++stack_it;
  }

  // AddressSpace
  llvm::errs() << "-- address space:\n";
  for (typeof(state.addressSpace.objects.begin()) it=state.addressSpace.objects.begin(), 
       ie=state.addressSpace.objects.end(); ie != it;  ++it) {
    ObjectState *os = it->second;
    const MemoryObject *mo = it->first;
    //if (os->getCopyOnWriteOwner() > 1) {
    //if (os->getUpdates().root) {
    if (mo->name != "unnamed") {
      llvm::errs() << "-- MemoryObject: " << mo->name << " " << mo->address << " --\n";
      os->print();
      llvm::errs() << "\n";
    }
  }
  llvm::errs() << "-- constraints:\n";
  std::string cvc_constraints;
  executor.getConstraintLog(state, cvc_constraints, false);
  llvm::errs() << cvc_constraints << "\n";
 
}

void NuklearFunctionHandler::handleNuklearMakeSymbolic(ExecutionState &state,
                                         KInstruction *target,
                                         std::vector<ref<Expr> > &arguments) {

  assert(arguments.size()==2);

  ref<Expr> address = arguments[0];
  std::string name = readStringAtAddress(state, arguments[1]);

  nuklearManager.executeAssignSymbolic(state, target, name, address );
}

/* NUKLEAR KLEE end (ENTIRE FILE) */
