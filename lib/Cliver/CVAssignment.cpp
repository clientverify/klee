//===-- CVAssignement.cpp ---------------------------------------*- C++ -*-===//
//
// <insert license>
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//

#include "cliver/CVAssignment.h"
#include "cliver/CVStream.h"
#include "cliver/Statistics.h"

#include "klee/util/ExprUtil.h"
#include "klee/TimerStatIncrementer.h"
#include "klee/util/ExprPPrinter.h"
#include "klee/Constraints.h"
#include "../Core/ImpliedValue.h"
#include "../Core/Common.h"

#include "llvm/Support/CommandLine.h"

////////////////////////////////////////////////////////////////////////////////

llvm::cl::opt<bool>
DebugCVAssignment("debug-cv-assignment", llvm::cl::init(false));

#ifndef NDEBUG

#undef CVDEBUG
#define CVDEBUG(x) \
	__CVDEBUG(DebugCVAssignment, x);

#undef CVDEBUG_S
#define CVDEBUG_S(__state_id, __x) \
	__CVDEBUG_S(DebugCVAssignment, __state_id, __x)

#endif


////////////////////////////////////////////////////////////////////////////////

namespace cliver {

CVAssignment::CVAssignment(std::vector<const klee::Array*> &objects, 
                           std::vector< std::vector<unsigned char> > &values) {
  addBindings(objects, values);
}

void CVAssignment::addBindings(std::vector<const klee::Array*> &objects, 
                                std::vector< std::vector<unsigned char> > &values) {

  std::vector< std::vector<unsigned char> >::iterator valIt = 
    values.begin();
  for (std::vector<const klee::Array*>::iterator it = objects.begin(),
          ie = objects.end(); it != ie; ++it) {
    const klee::Array *os = *it;
    std::vector<unsigned char> &arr = *valIt;
    bindings.insert(std::make_pair(os, arr));
    name_bindings.insert(std::make_pair(os->name, os));
    ++valIt;
  }
}

void CVAssignment::solveForBindings(klee::Solver* solver, 
                                    klee::ref<klee::Expr> &expr) {
  klee::TimerStatIncrementer timer(stats::bindings_solve_time);
  std::vector<const klee::Array*> arrays;
  std::vector< std::vector<unsigned char> > initial_values;

  klee::findSymbolicObjects(expr, arrays);
  klee::ConstraintManager cm;
  cm.addConstraint(expr);

  klee::Query query(cm, klee::ConstantExpr::alloc(0, klee::Expr::Bool));

  if (DebugCVAssignment) {
    llvm::errs() << " CVAssignment: ";
    klee::ExprPPrinter::printQuery(llvm::errs(), cm,
                             klee::ConstantExpr::alloc(0, klee::Expr::Bool));
    llvm::errs() << "\n";
  }

  solver->getInitialValues(query, arrays, initial_values);

  // Print implied values
	if (DebugCVAssignment) {
    for (unsigned i=0; i<arrays.size(); ++i) {
      std::ostringstream info;
      info << "IV: ARRAY(" << initial_values[i].size() << ") "
          << arrays[i]->name << " = ";

      bool isASCII = true;
      for (unsigned j=0; j<initial_values[i].size()-1; ++j) {
        if (initial_values[i][j] > 128)
          isASCII = false;
      }

      if (isASCII) {
        info << std::string(initial_values[i].begin(), initial_values[i].end());
      } else {
        info << "0x";
        for (unsigned j=0; j<initial_values[i].size(); ++j) {
          unsigned c = initial_values[i][j];
          info << std::hex << c << std::dec;
        }
      }

      CVDEBUG(info.str());
    }
  }

  klee::ref<klee::Expr> value_disjunction
		= klee::ConstantExpr::alloc(0, klee::Expr::Bool);

  for (unsigned i=0; i<arrays.size(); ++i) {
    for (unsigned j=0; j<initial_values[i].size(); ++j) {

      klee::ref<klee::Expr> read = 
        klee::ReadExpr::create(klee::UpdateList(arrays[i], 0),
          klee::ConstantExpr::create(j, klee::Expr::Int32));

      klee::ref<klee::Expr> neq_expr = 
        klee::NotExpr::create(
          klee::EqExpr::create(read,
            klee::ConstantExpr::create(initial_values[i][j], klee::Expr::Int8)));

      value_disjunction = klee::OrExpr::create(value_disjunction, neq_expr);
    }
  }

  // This may be a null-op how this interaction works needst to be better
  // understood
  // XXX FIXME also the failing cases below need to actually FAIL!
  value_disjunction = cm.simplifyExpr(value_disjunction);

  if (value_disjunction->getKind() == klee::Expr::Constant
      && cast<klee::ConstantExpr>(value_disjunction)->isFalse()) {
    CVDEBUG("Concretization must be true!");
    addBindings(arrays, initial_values);
  } else {

    cm.addConstraint(value_disjunction);

    bool result;
    solver->mayBeTrue(klee::Query(cm,
      klee::ConstantExpr::alloc(0, klee::Expr::Bool)), result);

    if (result) {
      cv_error("INVALID solver concretization!");
    } else {
      //TODO Test this path
      CVDEBUG("VALID solver concretization!");
      addBindings(arrays, initial_values);
    }
  }
}

void CVAssignment::print(std::ostream& os) const {
  for (name_ty::const_iterator it=name_bindings.begin(),ie=name_bindings.end();
       it!=ie; ++it) {
    os << it->first;
    name_ty::const_iterator next = it;
    if (++next != ie)
      os << ", ";
  }
}

////////////////////////////////////////////////////////////////////////////////

std::ostream &operator<<(std::ostream &os, const CVAssignment &x) {
  x.print(os);
  return os;
}

////////////////////////////////////////////////////////////////////////////////

} // end namespace cliver
