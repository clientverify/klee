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
    std::string s;
    llvm::raw_string_ostream info(s);
    info << "CVAssignment query:\n\n";
    klee::ExprPPrinter::printQuery(info, cm,
                             klee::ConstantExpr::alloc(0, klee::Expr::Bool));
    CVDEBUG(info.str());
  }

  solver->getInitialValues(query, arrays, initial_values);

  // Print implied values
	if (DebugCVAssignment) {
    for (unsigned i=0; i<arrays.size(); ++i) {
      std::ostringstream info;
      info << "IV: ARRAY(" << initial_values[i].size() << ") "
          << arrays[i]->name << " = ";

      bool is_printable = true;
      for (unsigned j=0; j<initial_values[i].size()-1; ++j) {
        if (!std::isprint(initial_values[i][j]))
          is_printable = false;
      }

      if (is_printable) {
        info << "\"";
        info << std::string(initial_values[i].begin(), initial_values[i].end());
        info << "\"";
      } else {
        info << "0x";
        for (unsigned j=0; j<initial_values[i].size(); ++j) {
          unsigned c = initial_values[i][j];
          info << std::setfill('0') << std::setw(2) << std::hex << c
               << std::dec;
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

  // This may be a null-op how this interaction works needs to be better
  // understood
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
  bool first_entry = true;
  for (const auto& entry: bindings) {
    const klee::Array* ar = entry.first;
    const std::vector<unsigned char>& data = entry.second;
    if (first_entry) {
      first_entry = false;
    } else {
      os << ", ";
    }
    // print array name and data bytes (little-endian)
    os << ar->name << "[" << data.size() << "] = 0x";
    for (size_t i = 0; i < data.size(); i++) {
      os << std::setfill('0') << std::setw(2) << std::hex << (int)data[i];
      os << std::dec;
    }
    // If possibly an integer or long, display decimal value.
    if (data.size() == sizeof(unsigned int)) {
      unsigned int integer_value;
      std::copy(data.begin(), data.end(), (unsigned char *)&integer_value);
      os << " (unsigned int " << integer_value << ")";
    } else if (data.size() == sizeof(unsigned long)) {
      unsigned long integer_value;
      std::copy(data.begin(), data.end(), (unsigned char *)&integer_value);
      os << " (unsigned long " << integer_value << ")";
    }
  }
}

////////////////////////////////////////////////////////////////////////////////

std::ostream &operator<<(std::ostream &os, const CVAssignment &x) {
  x.print(os);
  return os;
}

////////////////////////////////////////////////////////////////////////////////

} // end namespace cliver
