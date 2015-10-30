//===-- Constraints.cpp ---------------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "../Core/Common.h"
#include "klee/Constraints.h"
//#include "cliver/CliverStats.h"

#include "klee/util/Mutex.h"
#include "klee/util/ExprPPrinter.h"
#include "klee/util/ExprVisitor.h"
#include "klee/Internal/Support/Timer.h"
#if LLVM_VERSION_CODE >= LLVM_VERSION(3, 3)
#include "llvm/IR/Function.h"
#else
#include "llvm/Function.h"
#endif
#include "llvm/Support/CommandLine.h"
#include "klee/Internal/Module/KModule.h"

#include <map>

using namespace klee;

llvm::cl::opt<bool>
RewriteEqualities("rewrite-equalities",llvm::cl::init(false));

class ExprReplaceVisitor : public ExprVisitor {
private:
  ref<Expr> src, dst;

public:
  ExprReplaceVisitor(ref<Expr> _src, ref<Expr> _dst) : src(_src), dst(_dst) {}

  Action visitExpr(const Expr &e) {
    if (e == *src.get()) {
      return Action::changeTo(dst);
    } else {
      return Action::doChildren();
    }
  }

  Action visitExprPost(const Expr &e) {
    if (e == *src.get()) {
      return Action::changeTo(dst);
    } else {
      return Action::doChildren();
    }
  }
};

class ExprReplaceVisitor2 : public ExprVisitor {
private:
  const std::map< ref<Expr>, ref<Expr> > &replacements;

public:
  ExprReplaceVisitor2(const std::map< ref<Expr>, ref<Expr> > &_replacements) 
    : ExprVisitor(true),
      replacements(_replacements) {}

  Action visitExprPost(const Expr &e) {
    std::map< ref<Expr>, ref<Expr> >::const_iterator it =
      replacements.find(ref<Expr>(const_cast<Expr*>(&e)));
    if (it!=replacements.end()) {
      return Action::changeTo(it->second);
    } else {
      return Action::doChildren();
    }
  }
};

class ExprHashVisitor : public ExprVisitor {
  ExprHashSet visited_set;

public:
  ExprHashVisitor() : ExprVisitor(true) {}
  ExprHashSet& getExprHashSet() {
    return visited_set;
  }
  Action visitExprPost(const Expr &e) {
    if (!isa<ConstantExpr>(e))
      visited_set.insert(ref<Expr>(const_cast<Expr*>(&e)));
    return Action::doChildren();
  }
};

class ExprHashValVisitor : public ExprVisitor {
  std::vector<unsigned> visited_list;

public:
  ExprHashValVisitor() : ExprVisitor(true) {}
  std::vector<unsigned>& getVisited() {
    return visited_list;
  }
  Action visitExprPost(const Expr &e) {
    if (!isa<ConstantExpr>(e))
      visited_list.push_back(e.hash());
    return Action::doChildren();
  }
};

ConstraintManager::ConstraintManager(const std::vector< ref<Expr> > &_constraints)
  : constraints(_constraints) {
  equalities_map.clear();
  equalities_hash_map.clear();
  equalities_hashval_map.clear();
  for (auto e : constraints) {
    addToEqualitiesMap(e);
  }
}

ConstraintManager::ConstraintManager(const ConstraintManager &cs) 
  : constraints(cs.constraints), 
    equalities_map(cs.equalities_map),
    equalities_hash_map(cs.equalities_hash_map),
    equalities_hashval_map(cs.equalities_hashval_map) {}

bool ConstraintManager::rewriteConstraints(ExprVisitor &visitor) {
  ConstraintManager::constraints_ty old;
  bool changed = false;
  klee::WallTimer timer;

  constraints.swap(old);
  for (ConstraintManager::constraints_ty::iterator 
         it = old.begin(), ie = old.end(); it != ie; ++it) {
    ref<Expr> &ce = *it;
    ref<Expr> e = visitor.visit(ce);

    if (e!=ce) {
      addConstraintInternal(e); // enable further reductions
      changed = true;
    } else {
      constraints.push_back(ce);
    }
  }

  if (changed) {
    equalities_hash_map.clear();
    for (auto& e : constraints) {
      addToEqualitiesMap(e);
    }
  }

  //stats::rewrite_time += timer.check();
  return changed;
}

void ConstraintManager::simplifyForValidConstraint(ref<Expr> e) {
  // XXX 
}

void ConstraintManager::addToEqualitiesMap(ref<Expr> e) {
  //if (const EqExpr *ee = dyn_cast<EqExpr>(e)) {
  //  if (isa<ConstantExpr>(ee->left)) {
  //    equalities_hash_map.insert(std::make_pair(ee->right, ee->left));
  //  }
  //  equalities_hash_map.insert(std::make_pair(e,ConstantExpr::alloc(1, Expr::Bool)));
  //} else {
  //  equalities_hash_map.insert(std::make_pair(e,ConstantExpr::alloc(1, Expr::Bool)));
  //}

  if (const EqExpr *ee = dyn_cast<EqExpr>(e)) {
    if (isa<ConstantExpr>(ee->left)) {
      equalities_hashval_map.insert(std::make_pair(ee->right->hash(),
                                                   std::make_pair(ee->right, ee->left)));
    }
    equalities_hashval_map.insert(std::make_pair(e->hash(),
                                                 std::make_pair(e,ConstantExpr::alloc(1, Expr::Bool))));
  } else {
    equalities_hashval_map.insert(std::make_pair(e->hash(),
                                                 std::make_pair(e,ConstantExpr::alloc(1, Expr::Bool))));
  }

  if (const EqExpr *ee = dyn_cast<EqExpr>(e)) {
    if (isa<ConstantExpr>(ee->left)) {
      equalities_map.insert(std::make_pair(ee->right, ee->left));
    } else {
      equalities_map.insert(std::make_pair(e,ConstantExpr::alloc(1, Expr::Bool)));
    }
  } else {
    equalities_map.insert(std::make_pair(e,ConstantExpr::alloc(1, Expr::Bool)));
  }
}
ref<Expr> ConstraintManager::simplifyExprV4(ref<Expr> e) const {
  if (isa<ConstantExpr>(e))
    return e;

  ref<Expr> res = e;
  //klee::WallTimer timer;

  // Fast path, we have a direct assignment in the map
  auto it = equalities_hashval_map.find(e->hash());
  if (it != equalities_hashval_map.end() && it->second.first == e) {
    res = it->second.second;
  } else {
    // Slow path
    res = ExprReplaceVisitor2(equalities_map).visit(e);
  }

  //stats::simplify_expr_time_v4 += timer.check();
  return res;
}


ref<Expr> ConstraintManager::simplifyExprV3(ref<Expr> e) const {
  if (isa<ConstantExpr>(e))
    return e;

  ref<Expr> res = e;
  klee::WallTimer timer;

  // Fast path, we have a direct assignment in the map
  auto it = equalities_hashval_map.find(e->hash());
  if (it != equalities_hashval_map.end() && it->second.first == e) {
    res = it->second.second;
  } else {
    // Slow path, check if we can simplify

    bool can_simplify = false;
    ExprHashValVisitor ehv;
    ehv.visit(e);
    auto& visited = ehv.getVisited();
    for (auto v : visited) {
      auto it2 = equalities_hashval_map.find(v);
      if (it2 != equalities_hashval_map.end()) {
        can_simplify = true;
        break;
      }
    }

    if (can_simplify) {
      res = ExprReplaceVisitor2(equalities_map).visit(e);
    }
  }

  //stats::simplify_expr_time_v3 += timer.check();
  return res;
}

ref<Expr> ConstraintManager::simplifyExprV2(ref<Expr> e) const {
  if (isa<ConstantExpr>(e))
    return e;

  ref<Expr> res = e;
  klee::WallTimer timer;

  // Fast path, we have a direct assignment in the map
  auto it = equalities_hash_map.find(e);
  if (it != equalities_hash_map.end()) {
    //stats::simplify_expr_time_v2 += timer.check();
    //return it->second;
    res = it->second;
  } else {
    // Slow path, check if we can simplify

    bool can_simplify = false;
    ExprHashVisitor ehv;
    ehv.visit(e);
    ExprHashSet &e_set = ehv.getExprHashSet();
    for (auto& he : e_set) {
      auto it2 = equalities_hash_map.find(he);
      if (it2 != equalities_hash_map.end()) {
        can_simplify = true;
        break;
      }
    }

    if (can_simplify) {
      res = ExprReplaceVisitor2(equalities_map).visit(e);
    }
  }

  //stats::simplify_expr_time_v2 += timer.check();
  return res;
}


ref<Expr> ConstraintManager::simplifyExpr(ref<Expr> e) const {
  if (isa<ConstantExpr>(e))
    return e;

  ref<Expr> res4 = simplifyExprV4(e);
  return res4;

  ref<Expr> res2 = simplifyExprV2(e);
  ref<Expr> res3 = simplifyExprV3(e);
  //stats::simplify_expr_time += timer.check();
  //return res2;
  ref<Expr> res;
  klee::WallTimer timer;
 
  std::map< ref<Expr>, ref<Expr> > equalities;
  for (ConstraintManager::constraints_ty::const_iterator 
         it = constraints.begin(), ie = constraints.end(); it != ie; ++it) {
    if (const EqExpr *ee = dyn_cast<EqExpr>(*it)) {
      if (isa<ConstantExpr>(ee->left)) {
        equalities.insert(std::make_pair(ee->right,
                                         ee->left));
      } else {
        equalities.insert(std::make_pair(*it,
                                         ConstantExpr::alloc(1, Expr::Bool)));
      }
    } else {
      equalities.insert(std::make_pair(*it,
                                       ConstantExpr::alloc(1, Expr::Bool)));
    }
  }

  //return ExprReplaceVisitor2(equalities).visit(e);
  res = ExprReplaceVisitor2(equalities).visit(e);

  //stats::simplify_expr_time += timer.check();

  static Mutex lock;
  //if (did_simplify) {
  //  LockGuard guard(lock);
  //  llvm::errs() << "Simplified: ";
  //  klee::ExprPPrinter::printSingleExpr(llvm::errs(), e);
  //  llvm::errs() << " to ";
  //  klee::ExprPPrinter::printSingleExpr(llvm::errs(), res);
  //  llvm::errs() << "\n";
  //}
  if (res != res2 || res != res3 || res != res4) {
    {
      klee_warning("Mismatch simplify check: ");
      LockGuard guard(lock);
      llvm::errs() << "Simplified: ";
      klee::ExprPPrinter::printSingleExpr(llvm::errs(), e);
      llvm::errs() << "\nto (v1) ";
      klee::ExprPPrinter::printSingleExpr(llvm::errs(), res);
      llvm::errs() << "\nand (v2) ";
      klee::ExprPPrinter::printSingleExpr(llvm::errs(), res2);
      llvm::errs() << "\nand (v3) ";
      klee::ExprPPrinter::printSingleExpr(llvm::errs(), res3);
      llvm::errs() << "\nand (v4) ";
      klee::ExprPPrinter::printSingleExpr(llvm::errs(), res4);
      llvm::errs() << "\n";
      llvm::errs() << "All Constraints:\n";
      for (auto it : constraints) {
        klee::ExprPPrinter::printSingleExpr(llvm::errs(), it);
        llvm::errs() << "\n";
      }
      llvm::errs() << "All Equalities:\n";
      for (auto it : equalities_hash_map) {
        klee::ExprPPrinter::printSingleExpr(llvm::errs(), it.first);
        llvm::errs() << " --> ";
        klee::ExprPPrinter::printSingleExpr(llvm::errs(), it.second);
        llvm::errs() << "\n";
      }
    }
    exit(1);
  }
  return res;
}

#if 0
ref<Expr> ConstraintManager::simplifyExpr(ref<Expr> e) const {
  if (isa<ConstantExpr>(e))
    return e;

  klee::WallTimer timer;
  std::map< ref<Expr>, ref<Expr> > equalities;
  ref<Expr> res = e;
  ref<Expr> res2;

  //////ExprHashMap< ref<Expr> >& getExprHashMap() {
  //ExprHashMap< ref<Expr> > expr_map;
  //for (auto ce : constraints) {
  //  if (const EqExpr *ee = dyn_cast<EqExpr>(ce)) {
  //    if (isa<ConstantExpr>(ee->left)) {
  //      expr_map.insert(std::make_pair(ee->right,
  //                                       ee->left));
  //    } else {
  //      expr_map.insert(std::make_pair(ce,
  //                                       ConstantExpr::alloc(1, Expr::Bool)));
  //    }
  //  } else {
  //    expr_map.insert(std::make_pair(ce,
  //                                     ConstantExpr::alloc(1, Expr::Bool)));
  //  }
  //}
  
  bool can_simplify = false;
  //ExprHashVisitor ehv;
  //ehv.visit(e);
  //ExprHashSet &e_set = ehv.getExprHashSet();
  //for (auto he : e_set) {
  //  //auto it = expr_map.find(he);
  //  //if (it != expr_map.end()) {
  //  auto it = equalities_hash_map.find(he);
  //  if (it != equalities_hash_map.end()) {
  //    can_simplify = true;
  //  }
  //}

  auto it = equalities_hash_map.find(e);
  if (it != equalities_hash_map.end()) {
    can_simplify = true;
    res = it->second;
  }
 
  for (ConstraintManager::constraints_ty::const_iterator 
         it = constraints.begin(), ie = constraints.end(); it != ie; ++it) {
    if (const EqExpr *ee = dyn_cast<EqExpr>(*it)) {
      if (isa<ConstantExpr>(ee->left)) {
        equalities.insert(std::make_pair(ee->right,
                                         ee->left));
      } else {
        equalities.insert(std::make_pair(*it,
                                         ConstantExpr::alloc(1, Expr::Bool)));
      }
    } else {
      equalities.insert(std::make_pair(*it,
                                       ConstantExpr::alloc(1, Expr::Bool)));
    }
  }

  //return ExprReplaceVisitor2(equalities).visit(e);
  res2 = ExprReplaceVisitor2(equalities).visit(e);
  //stats::simplify_expr_time += timer.check();
  bool did_simplify = false;
  if (res2 != e) {
    did_simplify = true;
  }
  static Mutex lock;
  //if (did_simplify) {
  //  LockGuard guard(lock);
  //  llvm::errs() << "Simplified: ";
  //  klee::ExprPPrinter::printSingleExpr(llvm::errs(), e);
  //  llvm::errs() << " to ";
  //  klee::ExprPPrinter::printSingleExpr(llvm::errs(), res);
  //  llvm::errs() << "\n";
  //}
  if (can_simplify != did_simplify) { 
    {
      klee_warning("Mismatch simplify check: %d %d", can_simplify, did_simplify);
      LockGuard guard(lock);
      llvm::errs() << "Simplified: ";
      klee::ExprPPrinter::printSingleExpr(llvm::errs(), e);
      llvm::errs() << " to ";
      klee::ExprPPrinter::printSingleExpr(llvm::errs(), res2);
      llvm::errs() << "\n";
      llvm::errs() << "All Constraints:\n";
      for (auto it : constraints) {
        klee::ExprPPrinter::printSingleExpr(llvm::errs(), it);
        llvm::errs() << "\n";
      }
      llvm::errs() << "All Equalities:\n";
      for (auto it : equalities_hash_map) {
        klee::ExprPPrinter::printSingleExpr(llvm::errs(), it.first);
        llvm::errs() << " --> ";
        klee::ExprPPrinter::printSingleExpr(llvm::errs(), it.second);
        llvm::errs() << "\n";
      }

    }
  

    exit(1);
  }
  if (can_simplify) {
    if (res != res2) {
      klee_warning("Mismatch simplify :(");
      exit(1);
    }
  }
  return res;
}
#endif

void ConstraintManager::addConstraintInternal(ref<Expr> e) {
  // rewrite any known equalities 

  // XXX should profile the effects of this and the overhead.
  // traversing the constraints looking for equalities is hardly the
  // slowest thing we do, but it is probably nicer to have a
  // ConstraintSet ADT which efficiently remembers obvious patterns
  // (byte-constant comparison).

  switch (e->getKind()) {
  case Expr::Constant:
    assert(cast<ConstantExpr>(e)->isTrue() && 
           "attempt to add invalid (false) constraint");
    break;
    
    // split to enable finer grained independence and other optimizations
  case Expr::And: {
    BinaryExpr *be = cast<BinaryExpr>(e);
    addConstraintInternal(be->left);
    addConstraintInternal(be->right);
    break;
  }

  case Expr::Eq: {
    if (RewriteEqualities) {
      BinaryExpr *be = cast<BinaryExpr>(e);
      if (isa<ConstantExpr>(be->left)) {
        ExprReplaceVisitor visitor(be->right, be->left);
        rewriteConstraints(visitor);
      }
    }
    addToEqualitiesMap(e);
    constraints.push_back(e);
    break;
  }
    
  default:
    addToEqualitiesMap(e);
    constraints.push_back(e);
    break;
  }
}

void ConstraintManager::addConstraint(ref<Expr> e) {
  e = simplifyExpr(e);
  addConstraintInternal(e);
}
