//===-- LazyConstraint.cpp --------------------------------------*- C++ -*-===//
//
// <insert license>
//
//===----------------------------------------------------------------------===//
// 
// Implementation of the LazyConstraint and LazyConstraintDispatcher classes
//
//===----------------------------------------------------------------------===//

#include "cliver/CVStream.h"
#include "CVCommon.h"

#include "../Core/AddressSpace.h"
#include "../Core/Context.h"
#include "../Core/Memory.h"

#include "klee/util/ExprUtil.h"
#include "klee/util/ExprPPrinter.h"
#include "klee/Constraints.h"

#if LLVM_VERSION_CODE >= LLVM_VERSION(3, 3)
#include "llvm/IR/Function.h"
#else
#include "llvm/Function.h"
#endif

#include "llvm/Support/CommandLine.h"
#include "llvm/Support/raw_ostream.h"

#include "cliver/LazyConstraint.h"

#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

using namespace klee; // make life easier for this file (so many exprs!)

namespace cliver {

llvm::cl::opt<bool>
DebugLazyConstraint("debug-lazy-constraint", llvm::cl::init(false));

#ifndef NDEBUG

#undef CVDEBUG
#define CVDEBUG(x) \
  __CVDEBUG(DebugLazyConstraint, x);

#undef CVDEBUG_S
#define CVDEBUG_S(__state_id, __x) \
  __CVDEBUG_S(DebugLazyConstraint, __state_id, __x)

#undef CVDEBUG_S2
#define CVDEBUG_S2(__state_id_1, __state_id_2, __x) \
  __CVDEBUG_S2(DebugLazyConstraint, __state_id_1, __state_id_2, __x) \

#else

#undef CVDEBUG
#define CVDEBUG(x)

#undef CVDEBUG_S
#define CVDEBUG_S(__state_id, __x)

#undef CVDEBUG_S2
#define CVDEBUG_S2(__state_id_1, __state_id_2, __x)

#endif

////////////////////////////////////////////////////////////////////////////////

/// \brief Create expressions from assignment and add to a constraint manager.
/// \pre Assignment "as" must not contradict constraints in cm.
void addAssignmentToConstraints(const Assignment &as, ConstraintManager &cm) {
  for (const auto &kv : as.bindings) {
    const Array *ar = kv.first;
    const std::vector<unsigned char> &data = kv.second;
    assert(ar->size == data.size());
    for (size_t i = 0; i < data.size(); i++) {
      // FIXME: do we need to worry about non-empty update lists?
      UpdateList ar_ul(ar, 0);
      ref<Expr> read_byte_i =
          ReadExpr::create(ar_ul, ConstantExpr::create(i, Expr::Int32));
      ref<Expr> value = ConstantExpr::create(data[i], Expr::Int8);
      ref<Expr> byte_constraint = EqExpr::create(read_byte_i, value);
      cm.addConstraint(byte_constraint);
    }
  }
}

/// \brief Create one large expression from assignment
ref<Expr> assignmentToExpr(const Assignment &as) {
  ref<Expr> conjunct = ConstantExpr::alloc(1U, Expr::Bool);
  for (const auto &kv : as.bindings) {
    const Array *ar = kv.first;
    const std::vector<unsigned char> &data = kv.second;
    assert(ar->size == data.size());
    for (size_t i = 0; i < data.size(); i++) {
      // FIXME: do we need to worry about non-empty update lists?
      UpdateList ar_ul(ar, 0);
      ref<Expr> read_byte_i =
          ReadExpr::create(ar_ul, ConstantExpr::create(i, Expr::Int32));
      ref<Expr> value = ConstantExpr::create(data[i], Expr::Int8);
      ref<Expr> byte_constraint = EqExpr::create(read_byte_i, value);
      conjunct = AndExpr::create(conjunct, byte_constraint);
    }
  }
  return conjunct;
}

/// \brief Create one large expression as conjunction of all expressions
ref<Expr> conjunctAllExpr(const std::vector<ref<Expr>> &vex) {
  ref<Expr> conjunct = ConstantExpr::alloc(1U, Expr::Bool);
  for (auto e : vex) {
    conjunct = AndExpr::create(conjunct, e);
  }
  return conjunct;
}

// Should this be in Expr.cpp?
std::string exprToString(ref<Expr> e) {
  std::string expr_string;
  llvm::raw_string_ostream out(expr_string);
  ExprPPrinter *epp = ExprPPrinter::create(out);
  epp->setForceNoLineBreaks(true);
  epp->print(e);
  return out.str();
}

bool solveForUniqueExprVec(Solver *solver, const ConstraintManager &cm,
                           const LazyConstraint::ExprVec &exprs,
                           std::vector<unsigned char> &unique_values) {
  assert(solver);

  // Make a local copy of ConstraintManager
  ConstraintManager cm_temp(cm);

  // Make a dummy array (variable) and set bytewise equal to the elements of
  // exprs.  This is the variable we will try to solve for.
  const Array *dumdum = Array::CreateArray("dumdum", exprs.size());
  UpdateList ul(dumdum, 0);
  for (size_t i = 0; i < exprs.size(); i++) {
    ref<Expr> d = ReadExpr::create(ul, ConstantExpr::create(i, Expr::Int32));
    ref<Expr> e = exprs[i];
    ref<Expr> byte_constraint = EqExpr::create(d, e);
    cm_temp.addConstraint(byte_constraint);
  }

  // Solve for a satisfying assignment to the dummy array (may not be unique).
  // As a quirk of the solver system, we actually request a *counterexample* to
  // the expression "constraints => false", i.e., an assignment to dumdum that
  // enables constraints=true.
  Query query(cm_temp, ConstantExpr::alloc(0, Expr::Bool));
  std::vector<const Array *> arrays;
  arrays.push_back(dumdum);
  std::vector< std::vector<unsigned char> > initial_values;
  bool result = solver->getInitialValues(query, arrays, initial_values);
  if (!result) {
    CVDEBUG("No satisfying assignment to the expression vector.");
    return false;
  }

  // Now, check to see whether the assignment is the *unique* solution.
  Assignment as(arrays, initial_values, true);
  ref<Expr> as_expr = assignmentToExpr(as);
  solver->mustBeTrue(Query(cm_temp, as_expr), result);
  if (!result) {
    CVDEBUG("Expression vector is not uniquely determined.");
    return false;
  }

  // Copy the unique solution into the output vector.
  assert(initial_values.size() == 1U);
  unique_values = initial_values[0];

  return true;
}

////////////////////////////////////////////////////////////////////////////////

bool LazyConstraint::trigger(Solver *solver, const ConstraintManager &cm,
                             const Assignment &as,
                             std::vector<ref<Expr>> &new_constraints) const {
  assert(solver);

  // Make a local copy of ConstraintManager to avoid messing up the original.
  ConstraintManager cm_temp(cm);

  // Add assignment to CM.  WARNING: if assignment contradicts constraints in
  // CM, this may hit an assert and abort the program.
  addAssignmentToConstraints(as, cm_temp);

  // Concretize input expressions (abort if failure).  Use the specified solver
  // to find unique, concrete values for in_exprs.  Note that simplifyExpr()
  // fails to concretize in many cases because the expression simplification
  // heuristics are insufficient.
  std::vector<unsigned char> in_bytes;
  bool result = solveForUniqueExprVec(solver, cm_temp, in_exprs, in_bytes);
  if (!result) {
    CVDEBUG("Lazy constraint not triggered: cannot concretize in_exprs");
    return false;
  }

  // Prepare C-style input and output buffers.
  unsigned char *in_buf = new unsigned char[in_exprs.size()];
  for (size_t i = 0; i < in_bytes.size(); i++) {
    in_buf[i] = in_bytes[i];
  }
  unsigned char *out_buf = new unsigned char[out_exprs.size()];

  // Run trigger function.
  int ret = trigger_func(in_buf, in_exprs.size(), out_buf, out_exprs.size());

  // Clean up C-style input and output buffers (use C++ containers).
  std::vector<unsigned char> out_bytes;
  for (size_t i = 0; i < out_exprs.size(); i++) {
    out_bytes.push_back(out_buf[i]);
  }
  delete[] out_buf;
  delete[] in_buf;

  // If successful, create output constraints f(InC) == OutE.
  if (ret == 0) {
    new_constraints.clear();
    for (size_t i = 0; i < out_bytes.size(); i++) {
      ref<Expr> value = ConstantExpr::alloc(out_bytes[i], Expr::Int8);
      ref<Expr> byte_constraint = EqExpr::create(out_exprs[i], value);
      new_constraints.push_back(byte_constraint);
    }
  } else {
    CVDEBUG("Lazy constraint trigger " << trigger_func_name << " failed.");
    return false;
  }

  return true;
}

bool LazyConstraint::trigger(Solver *solver, const ConstraintManager &cm,
                             std::vector<ref<Expr>> &new_constraints) const {
  Assignment as(true);
  return trigger(solver, cm, as, new_constraints);
}

bool LazyConstraint::trigger(Solver *solver, const Assignment &as,
                             std::vector<ref<Expr>> &new_constraints) const {
  ConstraintManager cm;
  return trigger(solver, cm, as, new_constraints);
}

////////////////////////////////////////////////////////////////////////////////

bool LazyConstraintDispatcher::triggerAll(Solver *solver,
    std::vector<ref<Expr>> &new_constraints,
    std::vector<std::shared_ptr<LazyConstraint>> &triggered,
    const ConstraintManager &cm, bool recursive) {

  // Clear output params
  new_constraints.clear();
  triggered.clear();

  // Local copy of constraint manager
  ConstraintManager cm_temp(cm);

  // Recursively... (although we implement it as a loop, not recursion)
  bool something_triggered = false;
  do {
    // Iterate through cache of lazy constraints and try to trigger them
    something_triggered = false;
    auto it = lazy_constraint_cache.begin();
    while (it != lazy_constraint_cache.end()) {

      // Attempt to trigger this lazy constraint.
      std::shared_ptr<LazyConstraint> lc(*it);
      std::vector<ref<Expr>> byte_constraints;
      if (lc->trigger(solver, cm_temp, byte_constraints)) {

        // Triggered: get the newly realized constraints.
        something_triggered = true;
        triggered.push_back(lc);
        ref<Expr> merged_constraints = conjunctAllExpr(byte_constraints);
        new_constraints.push_back(merged_constraints);

        // Check that the newly realized constraints are consistent with
        // existing ones.
        bool result;
        solver->mayBeTrue(Query(cm_temp, merged_constraints), result);
        if (result) {
          // Consistent: add newly realized constraints to constraint manager.
          for (auto e: byte_constraints) {
            cm_temp.addConstraint(e);
          }
        } else {
          // Inconsistent: quit now.
          CVDEBUG("Lazy constraint "
                  << (*it)->name()
                  << " triggered and produced a contradiction.");
          return false;
        }

        // Remove lc from cache (erase increments iterator)
        it = lazy_constraint_cache.erase(it);

      } else {

        // Not triggered: move on to the next lazy constraint.
        ++it;

      }

    } // END: Iterate through cache.
  } while (recursive && something_triggered);

  return true;
}

} // end namespace cliver
