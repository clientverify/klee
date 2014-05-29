//===-- TimingSolver.cpp --------------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "TimingSolver.h"
#include "Common.h"

#include "klee/Config/Version.h"
#include "klee/ExecutionState.h"
#include "klee/Solver.h"
#include "klee/Statistics.h"
#include "klee/TimerStatIncrementer.h"

#include "klee/util/ExprUtil.h"

#include "CoreStats.h"

#include "llvm/Support/Process.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/raw_os_ostream.h"

using namespace klee;
using namespace llvm;

/***/
namespace klee {
  cl::opt<bool>
  DebugPrintSolverVars("debug-print-solver-vars",
                        cl::init(false));

}

static std::string getExprVarStr(ref<Expr> expr) {
  std::vector<const klee::Array*> symbolic_objects;
  klee::findSymbolicObjects(expr, symbolic_objects);
  std::ostringstream info2;
  for (unsigned i=0; i<symbolic_objects.size(); ++i) {
    info2 << symbolic_objects[i]->name << ", ";
  }
  return info2.str();
}

bool TimingSolver::evaluate(const ExecutionState& state, ref<Expr> expr,
                            Solver::Validity &result) {
  RecursiveLockGuard guard(solverMutex);
  // Fast path, to avoid timer and OS overhead.
  if (ConstantExpr *CE = dyn_cast<ConstantExpr>(expr)) {
    result = CE->isTrue() ? Solver::True : Solver::False;
    return true;
  }
  TimerStatIncrementer timer(stats::solverTime);

  if (simplifyExprs)
    expr = state.constraints.simplifyExpr(expr);

  if (DebugPrintSolverVars && !isa<ConstantExpr>(expr)) {
    *klee_warning_stream << "TimingSolver::evaluate(): StackTrace\n";
    *klee_warning_stream << "Expr vars: " << getExprVarStr(expr) << "\n";
    llvm::raw_ostream *ros = new llvm::raw_os_ostream(*klee_warning_stream);
    state.dumpStack(*ros);
    delete ros;
  }

  bool success = solver->evaluate(Query(state.constraints, expr), result);

  state.queryCost += timer.check()/1000000.;

  return success;
}

bool TimingSolver::mustBeTrue(const ExecutionState& state, ref<Expr> expr, 
                              bool &result) {
  RecursiveLockGuard guard(solverMutex);
  // Fast path, to avoid timer and OS overhead.
  if (ConstantExpr *CE = dyn_cast<ConstantExpr>(expr)) {
    result = CE->isTrue() ? true : false;
    return true;
  }
  TimerStatIncrementer timer(stats::solverTime);

  if (simplifyExprs)
    expr = state.constraints.simplifyExpr(expr);

  if (DebugPrintSolverVars && !isa<ConstantExpr>(expr)) {
    *klee_warning_stream << "TimingSolver::mustBeTrue(): StackTrace\n";
    std::string expr_string = getExprVarStr(expr);
    *klee_warning_stream << "Expr vars: " << expr_string << "\n";
    if (expr_string.size() == 0) {
      std::string str;
      llvm::raw_string_ostream ss(str);
      ss << "Expr: " << expr<< "\n";
      *klee_warning_stream << str;
    }
  }

  bool success = solver->mustBeTrue(Query(state.constraints, expr), result);

  state.queryCost += timer.check()/1000000.;

  return success;
}

bool TimingSolver::mustBeFalse(const ExecutionState& state, ref<Expr> expr,
                               bool &result) {
  RecursiveLockGuard guard(solverMutex);
  return mustBeTrue(state, Expr::createIsZero(expr), result);
}

bool TimingSolver::mayBeTrue(const ExecutionState& state, ref<Expr> expr, 
                             bool &result) {
  RecursiveLockGuard guard(solverMutex);
  bool res;
  if (!mustBeFalse(state, expr, res))
    return false;
  result = !res;
  return true;
}

bool TimingSolver::mayBeFalse(const ExecutionState& state, ref<Expr> expr, 
                              bool &result) {
  RecursiveLockGuard guard(solverMutex);
  bool res;
  if (!mustBeTrue(state, expr, res))
    return false;
  result = !res;
  return true;
}

bool TimingSolver::getValue(const ExecutionState& state, ref<Expr> expr, 
                            ref<ConstantExpr> &result) {
  RecursiveLockGuard guard(solverMutex);
  // Fast path, to avoid timer and OS overhead.
  if (ConstantExpr *CE = dyn_cast<ConstantExpr>(expr)) {
    result = CE;
    return true;
  }

  TimerStatIncrementer timer(stats::solverTime);

  if (simplifyExprs)
    expr = state.constraints.simplifyExpr(expr);

  if (DebugPrintSolverVars && !isa<ConstantExpr>(expr)) {
    *klee_warning_stream << "TimingSolver::getValue(): StackTrace\n";
    std::string expr_string = getExprVarStr(expr);
    *klee_warning_stream << "Expr vars: " << expr_string << "\n";
    if (expr_string.size() == 0) {
      std::string str;
      llvm::raw_string_ostream ss(str);
      ss << "Expr: " << expr<< "\n";
      *klee_warning_stream << str;
    }
    llvm::raw_ostream *ros = new llvm::raw_os_ostream(*klee_warning_stream);
    state.dumpStack(*ros);
    delete ros;
  }

  bool success = solver->getValue(Query(state.constraints, expr), result);

  state.queryCost += timer.check()/1000000.;

  return success;
}

bool 
TimingSolver::getInitialValues(const ExecutionState& state, 
                               const std::vector<const Array*>
                                 &objects,
                               std::vector< std::vector<unsigned char> >
                                 &result) {
  RecursiveLockGuard guard(solverMutex);
  if (objects.empty())
    return true;

  TimerStatIncrementer timer(stats::solverTime);

  bool success = solver->getInitialValues(Query(state.constraints,
                                                ConstantExpr::alloc(0, Expr::Bool)), 
                                          objects, result);
  
  state.queryCost += timer.check()/1000000.;
  
  return success;
}

std::pair< ref<Expr>, ref<Expr> >
TimingSolver::getRange(const ExecutionState& state, ref<Expr> expr) {
  RecursiveLockGuard guard(solverMutex);
  return solver->getRange(Query(state.constraints, expr));
}
