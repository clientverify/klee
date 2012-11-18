//===-- CanonicalSolver.cpp -------------------------------------*- C++ -*-===//
//
// <insert license>
//
//===----------------------------------------------------------------------===//

#include "CVCommon.h"
#include "cliver/CVStream.h"

#include "klee/Solver.h"
#include "klee/Constraints.h"
#include "klee/Expr.h"
#include "klee/IncompleteSolver.h"
#include "klee/SolverImpl.h"
#include "klee/util/ExprVisitor.h"

#include "llvm/ADT/StringExtras.h"

#include <map>
#include <vector>

////////////////////////////////////////////////////////////////////////////////

namespace cliver {

typedef std::vector<const klee::Array*> ArrayVector;
typedef std::map<const klee::Array*, unsigned> ArrayIndexMap;
typedef std::map<size_t, ArrayVector> SizeArrayVectorMap;
typedef std::map<size_t, int> SizeIndexMap;
typedef std::set<const klee::Array*> ArraySet;

class CanonicalVisitor : public klee::ExprVisitor {
 private:

  ArrayIndexMap &array_index_map_;
  SizeIndexMap &size_lru_map_;
  SizeArrayVectorMap &canonical_array_map_;
  ArraySet &canonical_array_set_;

 public:

  CanonicalVisitor(ArrayIndexMap &array_index_map, 
                   SizeIndexMap &size_lru_map,
                   SizeArrayVectorMap &canonical_array_map,
                   ArraySet &canonical_array_set)
    : klee::ExprVisitor(true), 
      array_index_map_(array_index_map),
      size_lru_map_(size_lru_map),
      canonical_array_map_(canonical_array_map),
      canonical_array_set_(canonical_array_set) {}

  Action visitRead(const klee::ReadExpr &e) {
    if (e.updates.root != NULL) {

      if (canonical_array_set_.count(e.updates.root) != 0) {
        return Action::doChildren();
      }

      const klee::Array *array = e.updates.root;

      if (!array->isConstantArray()) {
        size_t sz = e.updates.root->size;

        if (array_index_map_.find(e.updates.root) == array_index_map_.end()) {

          if (size_lru_map_.find(sz) == size_lru_map_.end())
            size_lru_map_[sz] = -1;

          int lru_index = size_lru_map_[sz];

          // Check if an array of this size has been created but not yet used
          if ((lru_index + 1) < canonical_array_map_[sz].size()) {
            array = canonical_array_map_[sz][lru_index + 1];

          } else {
            // Create new array
            array = new klee::Array("v", sz);

            // Insert into data structures
            canonical_array_map_[sz].push_back(array);
            canonical_array_set_.insert(array);
          }

          size_lru_map_[sz] = array_index_map_[e.updates.root] = lru_index + 1;

        } else {
          array = canonical_array_map_[sz][array_index_map_[e.updates.root]];
        }
      }

      // Because extend() pushes a new UpdateNode onto the list, we need to walk
      // the list in reverse to rebuild it in the same order.
      std::vector< const klee::UpdateNode*> update_list;
      for (const klee::UpdateNode *un = e.updates.head; un; un = un->next) {
        update_list.push_back(un);
      }

      // walk list in reverse
      klee::UpdateList updates(array, NULL);
      reverse_foreach (const klee::UpdateNode* U, update_list) {
        updates.extend(visit(U->index), visit(U->value));
      }

      return Action::changeTo(klee::ReadExpr::create(updates, visit(e.index)));
    }

    return Action::doChildren();
  }
};

////////////////////////////////////////////////////////////////////////////////

class CanonicalSolver : public klee::SolverImpl {
 private:
  
  klee::Solver *solver;
  SizeArrayVectorMap canonical_array_map_;
  ArraySet canonical_array_set_;

  void canonicalize(klee::ref<klee::Expr> &expr,
                    klee::ConstraintManager &constraints) {
    ArrayIndexMap array_index_map;
    SizeIndexMap size_lru_map;

    CanonicalVisitor visitor(array_index_map, size_lru_map, 
                             canonical_array_map_, canonical_array_set_);
    expr = visitor.visit(expr);

    for (klee::ConstraintManager::constraints_ty::iterator
          it = constraints.begin(), ie = constraints.end(); 
          it != ie; ++it) {
      *it = visitor.visit(*it);
    }
  }

 public:
  CanonicalSolver(klee::Solver *s) : solver(s) {


  }

  ~CanonicalSolver() { delete solver; }

  bool computeValidity(const klee::Query& query, 
                       klee::Solver::Validity &isValid) {
    klee::ref<klee::Expr> expr = query.expr;
    klee::ConstraintManager constraints(query.constraints);
    canonicalize(expr, constraints);
    klee::Query canonical_query(constraints, expr);
    return solver->impl->computeValidity(canonical_query, isValid);
  }

  bool computeTruth(const klee::Query& query, bool &result) {
    klee::ref<klee::Expr> expr = query.expr;
    klee::ConstraintManager constraints(query.constraints);
    canonicalize(expr, constraints);
    klee::Query canonical_query(constraints, expr);
    return solver->impl->computeTruth(canonical_query, result);
  }

  bool computeValue(const klee::Query& query, 
                    klee::ref<klee::Expr> &result) {
    klee::ref<klee::Expr> expr = query.expr;
    klee::ConstraintManager constraints(query.constraints);
    canonicalize(expr, constraints);
    klee::Query canonical_query(constraints, expr);
    return solver->impl->computeValue(canonical_query, result);
  }

  bool computeInitialValues(const klee::Query& query,
                            const std::vector<const klee::Array*> &objects,
                            std::vector< std::vector<unsigned char> > &values,
                            bool &hasSolution) {
    return solver->impl->computeInitialValues(query, objects, values, 
                                              hasSolution);
  }
};

klee::Solver *createCanonicalSolver(klee::Solver *_solver) {
  return new klee::Solver(new CanonicalSolver(_solver));
}

} // end namespace cliver
