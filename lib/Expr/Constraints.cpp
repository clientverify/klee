//===-- Constraints.cpp ---------------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "klee/Constraints.h"

#include "klee/util/ExprPPrinter.h"
#include "klee/util/ExprVisitor.h"
#include "klee/Internal/Module/KModule.h"

#include "llvm/IR/Function.h"
#include "llvm/Support/CommandLine.h"

#include <map>

using namespace klee;

namespace {
  llvm::cl::opt<bool>
  RewriteEqualities("rewrite-equalities",
		    llvm::cl::init(true),
		    llvm::cl::desc("Rewrite existing constraints when an equality with a constant is added (default=on)"));
}


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

bool ConstraintManager::rewriteConstraints(ExprVisitor &visitor) {
  ConstraintManager::constraints_ty old;
  bool changed = false;

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

  return changed;
}

void ConstraintManager::simplifyForValidConstraint(ref<Expr> e) {
  // XXX 
}

ref<Expr> ConstraintManager::simplifyExpr(ref<Expr> e) const {
  if (isa<ConstantExpr>(e))
    return e;

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

  return ExprReplaceVisitor2(equalities).visit(e);
}

void ConstraintManager::addConstraintInternal(ref<Expr> e) {
  // rewrite any known equalities and split Ands into different conjuncts

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
      // XXX: should profile the effects of this and the overhead.
      // traversing the constraints looking for equalities is hardly the
      // slowest thing we do, but it is probably nicer to have a
      // ConstraintSet ADT which efficiently remembers obvious patterns
      // (byte-constant comparison).
      BinaryExpr *be = cast<BinaryExpr>(e);
      if (isa<ConstantExpr>(be->left)) {
	ExprReplaceVisitor visitor(be->right, be->left);
	rewriteConstraints(visitor);
      }
    }
    constraints.push_back(e);
    break;
  }
    
  default:
    constraints.push_back(e);
    break;
  }
}

void ConstraintManager::addConstraint(ref<Expr> e) {
  e = simplifyExpr(e);
  addConstraintInternal(e);
}

//XOR optimization from cliver:

class ExtractExprVisitor : public ExprVisitor {
public:
  ExtractExprVisitor() {}

  Action visitExtract(const ExtractExpr &extractExpr) {
    auto e = extractExpr.getKid(0);

    if (e->getNumKids() == 2) {
      switch (e->getKind()) {
      case Expr::Xor: {
        ref<Expr> left = ExtractExpr::create(e->getKid(0), extractExpr.offset,
                                             extractExpr.width);
        ref<Expr> right = ExtractExpr::create(e->getKid(1), extractExpr.offset,
                                              extractExpr.width);
	ref<Expr> res = XorExpr::create(left, right);
        return Action::changeTo(res);

        break;
      }
      }
      return Action::doChildren();
    }
  }
};

class XorLiftVisitor : public ExprVisitor {
public:
  XorLiftVisitor() {}

  Action visitEq(const EqExpr &e) {
    if ((isa<ConstantExpr>(e.getKid(0)) && isa<XorExpr>(e.getKid(1)))) {
      auto xorExpr = e.getKid(1);
      if (isa<ConstantExpr>(xorExpr->getKid(0))) {
        // llvm::outs() << "we can optimize!\n";                                                                                                                      
        auto xorExprConstant = xorExpr->getKid(0);
        ref<Expr> left = XorExpr::create(xorExprConstant, e.getKid(0));
        ref<Expr> right = XorExpr::create(xorExprConstant, e.getKid(1));
        ref<Expr> res = EqExpr::create(left, right);
        return Action::changeTo(res);
      }
    }
    return Action::doChildren();
  }
};

class XorPropagateVisitor : public ExprVisitor {
public:
  XorPropagateVisitor() {}

  Action visitXor(const XorExpr &e) {
    if ((isa<ConstantExpr>(e.getKid(0)) && isa<XorExpr>(e.getKid(1)))) {
      if (e.getKid(1)->getKid(0) == e.getKid(0)) {
        // llvm::outs() << "we can optimize!\n";                                                                                                                      
        return Action::changeTo(e.getKid(1)->getKid(1));
      }
    }
    return Action::doChildren();
  }
};

void ConstraintManager::DoXorOptimization() {

  ExtractExprVisitor v;
  XorLiftVisitor xl;
  XorPropagateVisitor xp;

  rewriteConstraints(v);
  rewriteConstraints(xl);
  rewriteConstraints(xp);
}

ref<Expr> ConstraintManager::simplifyWithXorOptimization(ref<Expr> e) const {

  ExtractExprVisitor v;
  XorLiftVisitor xl;
  XorPropagateVisitor xp;
  e = v.visit(e);
  e = xl.visit(e);
  e = xp.visit(e);
  return e;
}
