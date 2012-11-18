//===-- CanonicalSolver.cpp -------------------------------------*- C++ -*-===//
//
// <insert license>
//
//===----------------------------------------------------------------------===//

#include "klee/Solver.h"

#include "klee/Constraints.h"
#include "klee/Expr.h"
#include "klee/IncompleteSolver.h"
#include "klee/SolverImpl.h"

//using namespace klee;

namespace cliver {

class CanonicalSolver : public klee::SolverImpl {
 private:
  
  klee::Solver *solver;

 public:
  CanonicalSolver(klee::Solver *s) : solver(s) {}
  ~CanonicalSolver() { delete solver; }

  bool computeValidity(const klee::Query& query, 
                       klee::Solver::Validity &isValid) {
    return solver->impl->computeValidity(query, isValid);
  }

  bool computeTruth(const klee::Query& query, bool &result) {
    return solver->impl->computeTruth(query, result);
  }

  bool computeValue(const klee::Query& query, 
                    klee::ref<klee::Expr> &result) {
    return solver->impl->computeValue(query, result);
  }

  bool computeInitialValues(const klee::Query& query,
                            const std::vector<const klee::Array*> &objects,
                            std::vector< std::vector<unsigned char> > &values,
                            bool &hasSolution) {
    return solver->impl->computeInitialValues(query, objects, values, 
                                              hasSolution);
  }
};

///

klee::Solver *createCanonicalSolver(klee::Solver *_solver) {
  return new klee::Solver(new CanonicalSolver(_solver));
}

} // end namespace cliver
