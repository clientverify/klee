#include "constraint_set.h"

//#define DEBUG_TYPE "constraint-set"
#define DEBUG_TYPE "xpilot-checker"
#include "llvm/Support/Debug.h"
#include "llvm/Support/CommandLine.h"


using namespace klee;
using namespace llvm;

namespace {

  cl::opt<bool>
  NoImpliedUpdates("no-implied-updates",
		   cl::init(false));
}


//////////////////////////////////////////////////////////////////////////////
// ConstraintSet

ConstraintSet::ConstraintSet() 
  : store_constraints_(false), marked_(false), check_implied_(true) {}

ConstraintSet::ConstraintSet(const ConstraintSet &cs) :
  elements_(cs.elements_),
  whole_objects_(cs.whole_objects_),
  constraints_(cs.constraints_),
  store_constraints_(cs.store_constraints_),
  marked_(cs.marked_),
  check_implied_(true) {}    

// This constructor is used for intersection tests between
// ConstraintSets and MemoryUpdateSets.
ConstraintSet::ConstraintSet(const MemoryUpdateSet &us)
  : store_constraints_(false), marked_(false), check_implied_(true) {
  foreach(u, us.begin(), us.end()) {
    //shared_ptr<Array> mo = u->second->memory_object();
    Array *array = u->second->array();
    DenseSet<unsigned> &dis = elements_[array];
    foreach(value, u->second->begin(), u->second->end()) {
      dis.add(value->first);
    }
  }
  whole_objects_.clear();
}

ConstraintSet::ConstraintSet(ref<Expr> e, bool store_constraints) 
  : store_constraints_(store_constraints), marked_(false),
    check_implied_(true) {
  if (store_constraints_) {
#ifdef CV_USE_HASH
    constraints_.insert(e);
#else
    constraints_.push_back(e);
#endif
  }
  std::vector< ref<ReadExpr> > reads;
  findReads(e, /* visitUpdates= */ true, reads);
  foreach(it, reads.begin(), reads.end()) {
    ReadExpr *re = it->get();
    if (re->updates.root) {
      const Array *array = re->updates.root;
      if (!whole_objects_.count(array)) {
        if (ConstantExpr *CE = dyn_cast<ConstantExpr>(re->index)) {
          //llvm::cout << "CE->width: " << CE->getWidth() << "\n";
          DenseSet<unsigned> &dis = elements_[array];
          dis.add((unsigned) CE->getZExtValue());
        } else {
          let(it2, elements_.find(array));
          if (it2!=elements_.end())
            elements_.erase(it2);
          whole_objects_.insert(array);
        }
      }
    }
  }
}

ConstraintSet &ConstraintSet::operator=(const ConstraintSet &cs) {
  elements_ = cs.elements_;
  whole_objects_ = cs.whole_objects_;
  constraints_ = cs.constraints_;
  marked_ = cs.marked_;
  store_constraints_ = cs.store_constraints_;
  check_implied_ = true;
  return *this;
}

bool ConstraintSet::equals(const ConstraintSet &b) const {
  if (constraints_.size() != b.constraints_.size())
    return false;
#ifdef CV_USE_HASH
  foreach(e, b.constraints_.begin(), b.constraints_.end()) {
    let(it, constraints_.find(*e));
    if (it == constraints_.end())
      return false;
  }
  return true;
#else
  bool match = true;
  // check that every element in b is in constraints_
  foreach(e, constraints_.begin(), constraints_.end()) {
    match = false;
    foreach(b_e, b.constraints_.begin(), b.constraints_.end()) {
      if ((*e).compare(*b_e) == 0)
        match = true;
      if (match)
        break;
    }
    if (!match) 
      return match;
  }
  return match;
  // XXX need to also check elements_ and whole_objects_ ?
#endif
}

// Prints Array ids, but not constraints_.
void ConstraintSet::print(std::ostream &os) const {
  os << "{";
  bool first = true;
  foreach(it, whole_objects_.begin(), whole_objects_.end()) {
    //shared_ptr<Array> mo(*it);
    const Array *array = *it;
    if (first) {
      first = false;
    } else {
      os << ", ";
    }
    os << array->name << " : {WO}";
  }
  foreach(it, elements_.begin(), elements_.end()) {
    //shared_ptr<Array> mo(it->first);
    const Array *array = it->first;
    const DenseSet<unsigned> &dis = it->second;
    if (first) {
      first = false;
    } else {
      os << ", ";
    }
    //os << array->name << " : " << dis;
    os << array->name << " : " << "{DS}";
  }
  os << "}";

}

bool ConstraintSet::intersects(const MemoryUpdateSet& us) const {
  return intersects(ConstraintSet(us));
}

bool ConstraintSet::intersects(const ConstraintSet &b) const {
  // more efficient when this is the smaller set 
  if (elements_.size() > b.elements_.size()) return b.intersects(*this);

  foreach(it, whole_objects_.begin(), whole_objects_.end()) {
    //shared_ptr<Array> mo(*it);
    const Array *mo = *it;
    if (b.whole_objects_.count(mo) || b.elements_.find(mo)!=b.elements_.end())
      return true;
  }
  foreach(it, elements_.begin(), elements_.end()) {
    //shared_ptr<Array> mo(it->first);
    const Array *mo = it->first;
    if (b.whole_objects_.count(mo))
      return true;
    let(it2, b.elements_.find(mo));
    if (it2!=b.elements_.end()) {
      if (it->second.intersects(it2->second))
        return true;
    }
  }
  return false;
}

// returns true if set is changed by addition
bool ConstraintSet::add(const ConstraintSet &b) {
  check_implied_ = true;
  bool modified = false;
  foreach(it, b.whole_objects_.begin(), b.whole_objects_.end()) {
    //shared_ptr<Array> mo(*it);
    const Array *mo = *it;
    let(it2, elements_.find(mo));
    if (it2 != elements_.end()) {
      modified = true;
      elements_.erase(it2);
      whole_objects_.insert(mo);
    } else {
      if (!whole_objects_.count(mo)) {
        modified = true;
        whole_objects_.insert(mo);
      }
    }
  }
  foreach(it, b.elements_.begin(), b.elements_.end()) {
    //shared_ptr<Array> mo(it->first);
    const Array *mo = it->first;
    if (!whole_objects_.count(mo)) {
      let(it2, elements_.find(mo));
      if (it2 == elements_.end()) {
        modified = true;
        elements_.insert(*it);
      } else {
        if (it2->second.add(it->second))
          modified = true;
      }
    }
  }

  if (b.marked_) 
    marked_ = true;

  if (!b.store_constraints_) 
    store_constraints_ = false;

  // XXX: check for duplicates?
  if (store_constraints_) 
#ifdef CV_USE_HASH
    constraints_.insert(b.constraints_.begin(), b.constraints_.end());
#else
    constraints_.insert(constraints_.end(), 
                        b.constraints_.begin(), b.constraints_.end());
#endif

  return modified;
}

//===----------------------------------------------------------------------===//
// Class ConstraintSetFamily

ConstraintSetFamily::ConstraintSetFamily(std::vector< ref<Expr> > evec) 
  : valid_(true) {
  foreach(e, evec.begin(), evec.end()) {
    addConstraintInternal(*e, false);
  }
}

ConstraintSetFamily::ConstraintSetFamily(ref<Expr> e)
  : valid_(true) {
  addConstraintInternal(e, false);
}

bool ConstraintSetFamily::add(ref<Expr> e) {
  addConstraintInternal(e, false);
  return valid_;
}

void ConstraintSetFamily::addConstraintInternal(ref<Expr> _e, bool marked) {
  // If not valid, ConstraintSetFamily will not become valid, so no need to 
  // add new constraints. 
  if (valid_ == false) return;

  // record any array with update lists to be ignored when searching for implied
  // values

  std::vector< ref<Expr> > stack;
  std::vector< ref<Expr> > expr_vec;

  stack.push_back(_e);

  // Split any And expressions and place the left and right sides back
  // into the stack. Otherwise, each expr in the stack is
  // removed and placed into expr_vec, which represents a conjunction of
  // constraints. Any false expression (Constant == 0) will cause a short
  // circuit and return immediately.
  while (stack.size() > 0) {
    ref<Expr> e = stack.back();
    stack.pop_back();

    switch (e->getKind()) {

    case Expr::Constant: {
      if (cast<ConstantExpr>(e)->isFalse()) {
        valid_ = false;
        return;
      }
      break;
    }

    // split to enable finer grained independence and other optimizations
    case Expr::And: {
      BinaryExpr *be = cast<BinaryExpr>(e);
      stack.push_back(be->left);
      stack.push_back(be->right);
      break;
    }
      
    default:
      expr_vec.push_back(e);
      break;
    }
  }

  // For each expr in the expr_vec, we create a new ConstraintSet object, mark
  // it if marked==true and then check for intersection with any other
  // ConstraintSet in the constraint_sets_ vector. If an interesection is found,
  // it is merged with the intersecting object. Otherwise the ConstraintSet is
  // added to the constraint_sets_ vector.
  foreach(e, expr_vec.begin(), expr_vec.end()) {
    ConstraintSet cs_new(*e, true);
    if (marked) cs_new.set_mark(marked);
    constraint_sets_union_.add(cs_new); //optimize?
    bool intersects = false;
    foreach(cs, constraint_sets_.begin(), constraint_sets_.end()) {
      if (cs_new.intersects(*cs)) {
        (*cs).add(cs_new);
        intersects = true;
        break;
      }
    }
    if (!intersects) {
      constraint_sets_.push_back(cs_new);
    }
  }
}


void ConstraintSetFamily::print(std::ostream &os) const {
  if (implied_updates_.size() > 0) {
    os << implied_updates_;
    if (constraint_sets_.size() > 0)
      os << std::endl;
  }
  foreach(it, constraint_sets_.begin(), constraint_sets_.end()) {
    if (it != constraint_sets_.begin()) os << std::endl;
    os << *it << " =\n";
    foreach(it2, (*it).begin(), (*it).end()) {
      if (it2 != (*it).begin()) os << ", \n";
      os << *it2;
    }
  }

}
  
void ConstraintSetFamily::set_mark(bool m) {
  foreach(it, constraint_sets_.begin(), constraint_sets_.end()) {
    (*it).set_mark(m);
  }
}

bool ConstraintSetFamily::applyImpliedUpdates() { 
  return applyUpdates(implied_updates_);
  /*
  int apply_updates = true;
  int apply_updates_count = 0;
  while ( apply_updates && apply_updates_count < 5 
          && applyUpdates(implied_updates_) ) {
    if (checkForImpliedUpdates())
      apply_updates = true;
    else 
      apply_updates = false;

    apply_updates_count++;
  }
  llvm::cout << "applyImpliedUpdates count: " << apply_updates_count << "\n";
  return apply_updates_count;
  */
}

bool ConstraintSetFamily::applyUpdates(const MemoryUpdateSet &us) {
  TimeStatIncrementer timestat(fstats.time_ConstraintSetFamily_applyUpdates);
  if (NoImpliedUpdates)  return false;
  // invariant: updates can *only* split ConstraintSets, we do not
  //   need to recheck for intersection
  bool applied_updates = false;
  ConstraintSet memory_update_cs(us);
  // check if MemoryUpdateSet us constains relevant updates
  if (constraint_sets_union_.intersects(memory_update_cs)) {
    //DEBUG(llvm::cout << "applyUpdates - Intersection.\n");
    //DEBUG(llvm::cout << us << "\n");

    ConstraintGenerator<MemoryUpdateSet> 
      generator(const_cast<MemoryUpdateSet*>(&us));

    std::vector<std::pair<ref<Expr>,bool> > updated_cs_vec;
    std::vector<ConstraintSet> non_intersecting_cs_vec;

    foreach(cs, constraint_sets_.begin(), constraint_sets_.end()) {
      if ((*cs).intersects(memory_update_cs)) {
        // MemoryUpdateSet applies to this ConstraintSet, 
        // We rebuild the ConstraintSet because the updates may cut
        // the Expr graph it represents.
        bool marked = (*cs).get_mark();
        foreach(e, (*cs).begin(), (*cs).end()) {
          ConstraintSet tmp_cs(*e, false);
          // Check again for finer grained intersection.
          if (tmp_cs.intersects(memory_update_cs)) {
            // Generate ref<Expr> with updates applied.
            ref<Expr> e_updated = generator.visit(*e);
            applied_updates = true;
            if (ConstantExpr *CE = dyn_cast<ConstantExpr>(e_updated)) {
              if (CE->isFalse()) {
                // ConstraintSetFamily is now invalid, no need to continue.
                DEBUG(llvm::cout << "Failed in applyUpdates: " << *e << "\n");
                valid_ = false;
                return applied_updates;
              }
            } else {
              updated_cs_vec.push_back(std::make_pair(e_updated, marked));
            }
          } else {
            updated_cs_vec.push_back(std::make_pair(*e, marked));
          }
        }
      } else {
        // XXX erase intersectors rather than copy?
        non_intersecting_cs_vec.push_back(*cs);
      }
    }

    constraint_sets_.swap(non_intersecting_cs_vec);
    
    if (applied_updates && updated_cs_vec.size() > 0) {
      foreach(it, updated_cs_vec.begin(), updated_cs_vec.end()) {
        addConstraintInternal(it->first, it->second);
      }
    }
    // XXX rebuild constraint_sets_union?
  } else {
    //DEBUG(llvm::cout << "applyUpdates - No Intersection.\n");
    //DEBUG(llvm::cout << "CONSTRAINTSET: " << constraint_sets_union_ << "\n");
    ////DEBUG(llvm::cout << "CONSTRAINTSETS:\n");
    ////foreach (constraint_set, begin(), end())
    ////  DEBUG(llvm::cout << *constraint_set << "\n");
    //DEBUG(llvm::cout << "UPDATESET:\n" << us << "\n");
  }

  return applied_updates;
}

// Determine if e contains a ReadExpr with a non-NULL UpdateList
static bool hasUpdateList(ref<Expr> e) {
  std::vector< ref<Expr> > stack;
  ExprHashSet visited;
  
  if (!isa<ConstantExpr>(e)) {
    visited.insert(e);
    stack.push_back(e);
  }

  while (!stack.empty()) {
    ref<Expr> top = stack.back();
    stack.pop_back();

    if (ReadExpr *re = dyn_cast<ReadExpr>(top)) {
      if (!isa<ConstantExpr>(re->index) &&
          visited.insert(re->index).second)
        stack.push_back(re->index);
      
      if (re->updates.head) return true;
     
    } else if (!isa<ConstantExpr>(top)) {
      Expr *e = top.get();
      for (unsigned i=0; i<e->getNumKids(); i++) {
        ref<Expr> k = e->getKid(i);
        if (!isa<ConstantExpr>(k) &&
            visited.insert(k).second)
          stack.push_back(k);
      }
    }
  }
  return false;
}

void ConstraintSetFamily::getImpliedValues(ref<Expr> e,
                                    ref<ConstantExpr> value,
                                    ImpliedValueList &results) {
  switch (e->getKind()) {
  case Expr::Constant: {
    if (value != cast<ConstantExpr>(e)) {
      DEBUG(llvm::cout<<"getImpliedValues: " << value << " != " << e << "\n");
      valid_ = false;
    }
    //assert(value == cast<ConstantExpr>(e) && 
    //       "error in implied value calculation");
    break;
  }

    // Special

  case Expr::NotOptimized: break;

  case Expr::Read: {
    // XXX in theory it is possible to descend into a symbolic index
    // under certain circumstances (all values known, known value
    // unique, or range known, max / min hit). Seems unlikely this
    // would work often enough to be worth the effort.
    ReadExpr *re = cast<ReadExpr>(e);
    results.push_back(std::make_pair(re, value));
    break;
  }
    
  case Expr::Select: {
    // not much to do, could improve with range analysis
    SelectExpr *se = cast<SelectExpr>(e);
    
    if (ConstantExpr *TrueCE = dyn_cast<ConstantExpr>(se->trueExpr)) {
      if (ConstantExpr *FalseCE = dyn_cast<ConstantExpr>(se->falseExpr)) {
        if (TrueCE != FalseCE) {
          if (value == TrueCE) {
            getImpliedValues(se->cond, ConstantExpr::alloc(1, Expr::Bool), 
                             results);
          } else {
            assert(value == FalseCE &&
                   "err in implied value calculation");
            getImpliedValues(se->cond, ConstantExpr::alloc(0, Expr::Bool), 
                             results);
          }
        }
      }
    }
    break;
  }

  case Expr::Concat: {
    ConcatExpr *ce = cast<ConcatExpr>(e);
    getImpliedValues(ce->getKid(0), value->Extract(ce->getKid(1)->getWidth(),
                                                   ce->getKid(0)->getWidth()),
                     results);
    getImpliedValues(ce->getKid(1), value->Extract(0,
                                                   ce->getKid(1)->getWidth()),
                     results);
    break;
  }
    
  case Expr::Extract: {
    // XXX, could do more here with "some bits" mask
    break;
  }

    // Casting

  case Expr::ZExt: 
  case Expr::SExt: {
    CastExpr *ce = cast<CastExpr>(e);
    getImpliedValues(ce->src, value->Extract(0, ce->src->getWidth()),
                     results);
    break;
  }

    // Arithmetic

  case Expr::Add: { // constants on left
    BinaryExpr *be = cast<BinaryExpr>(e);
    // C_0 + A = C  =>  A = C - C_0
    if (ConstantExpr *CE = dyn_cast<ConstantExpr>(be->left))
      getImpliedValues(be->right, value->Sub(CE), results);
    break;
  }
  case Expr::Sub: { // constants on left
    BinaryExpr *be = cast<BinaryExpr>(e);
    // C_0 - A = C  =>  A = C_0 - C
    if (ConstantExpr *CE = dyn_cast<ConstantExpr>(be->left))
      getImpliedValues(be->right, CE->Sub(value), results);
    break;
  }
  case Expr::Mul: {
    // FIXME: Can do stuff here, but need valid mask and other things because of
    // bits that might be lost.
    break;
  }

  case Expr::UDiv:
  case Expr::SDiv:
  case Expr::URem:
  case Expr::SRem:
    break;

    // Binary

  case Expr::And: {
    BinaryExpr *be = cast<BinaryExpr>(e);
    if (be->getWidth() == Expr::Bool) {
      if (value->isTrue()) {
        getImpliedValues(be->left, value, results);
        getImpliedValues(be->right, value, results);
      }
    } else {
      // FIXME; We can propogate a mask here where we know "some bits". May or
      // may not be useful.
    }
    break;
  }
  case Expr::Or: {
    BinaryExpr *be = cast<BinaryExpr>(e);
    if (value->isZero()) {
      getImpliedValues(be->left, 0, results);
      getImpliedValues(be->right, 0, results);
    } else {
      // FIXME: Can do more?
    }
    break;
  }
  case Expr::Xor: {
    BinaryExpr *be = cast<BinaryExpr>(e);
    if (ConstantExpr *CE = dyn_cast<ConstantExpr>(be->left))
      getImpliedValues(be->right, value->Xor(CE), results);
    break;
  }

    // Comparison
  case Expr::Ne: 
    value = value->Not();
    /* fallthru */
  case Expr::Eq: {
    EqExpr *ee = cast<EqExpr>(e);
    if (value->isTrue()) {
      if (ConstantExpr *CE = dyn_cast<ConstantExpr>(ee->left))
        getImpliedValues(ee->right, CE, results);
    } else {
      // Look for limited value range.
      //
      // In general anytime one side was restricted to two values we can apply
      // this trick. The only obvious case where this occurs, aside from
      // booleans, is as the result of a select expression where the true and
      // false branches are single valued and distinct.
      
      if (ConstantExpr *CE = dyn_cast<ConstantExpr>(ee->left))
        if (CE->getWidth() == Expr::Bool)
          getImpliedValues(ee->right, CE->Not(), results);
    }
    break;
  }
    
  default:
    break;
  }
}

bool ConstraintSetFamily::checkForImpliedUpdates() {
  if (NoImpliedUpdates)  return false;
  TimeStatIncrementer timestat(fstats.time_ConstraintSetFamily_checkForImpliedUpdates);
  bool found_implied = false;
  foreach(cs, constraint_sets_.begin(), constraint_sets_.end()) {
    //if (true) { //(*cs).check_implied()) {
    if ((*cs).check_implied()) {
      (*cs).set_check_implied();
      // build conjunction of ref<Expr> in ConstraintSet cs
      ref<Expr> conj = vecExpr((*cs).begin(), (*cs).end(), Expr::And); 
      ImpliedValueList results;

      // check for constant valued conj
      if (ConstantExpr *CE_conj = dyn_cast<ConstantExpr>(conj)) {
        if (CE_conj->isFalse()) {
          valid_ = false;
          return true;
        }
      } else if (!hasUpdateList(conj)) {
        // get implied values of (conj => true)
        //DEBUG(llvm::cout << "Checking Implied Values of :\n" << conj << "\n");
        getImpliedValues(conj, ConstantExpr::create(1, Expr::Bool), results);
        if (!valid_) return false;

        int found_implied_count = 0;
        if (results.size() > 0) {
          foreach(it, results.begin(), results.end()) {
            // should always be true...
            if (ConstantExpr *CE_index = dyn_cast<ConstantExpr>(it->first.get()->index)) {
              if (ConstantExpr *CE_value = dyn_cast<ConstantExpr>(it->second)) {
                Array *array = const_cast<Array*>(it->first.get()->updates.root);
                int index = CE_index->getZExtValue();
                uint8_t value = CE_value->getZExtValue();
                //DEBUG(printf("IMPLIED: %s[%d] = %x\n", array->name.c_str(), index, value));
                shared_ptr<MemoryUpdate> mu = implied_updates_.find(array->name);
                if (mu.get()) {
                  // Check for conflicts with the updates we already have 
                  uint8_t old_value;
                  if (mu->find(index, old_value)) {
                    if (value != old_value) {
                      llvm::cout << "name: " << array->name << "\n"
                                 << "value: " << (uint64_t)value << "\n"
                                 << "oldvalue: " << (uint64_t)old_value << "\n";

                      llvm::cout << "ImpliedUpdates:\n" << implied_updates_ << "\n";
                    }
                    assert(value == old_value && "update conflict");
                  }
                }
                implied_updates_.add(array, array->name, index, value);
                found_implied_count++;
              }
            }
          }
        }
        //DEBUG(llvm::cout << found_implied_count << " implied updates found.\n");
      } else {
        //DEBUG(llvm::cout << "Not checking Implied Values of :\n" << conj << "\n");
      }
    }
  }
  //if (found_implied) applyImpliedUpdates();
  return found_implied;
}

bool ConstraintSetFamily::intersects(const MemoryUpdateSet &us) const {
  return constraint_sets_union_.intersects(us);
}

bool ConstraintSetFamily::add(const ConstraintSetFamily &csm) {
  TimeStatIncrementer timestat(fstats.time_ConstraintSetFamily_add);

  // apply this.implied_updates_ to csm and vice versa
  // then merge the vectors of constraint sets
  // maintain the implied_updates_ if current, and maintain
  // the constraints that currently are or will become current

  // what is the best order to apply updates and merge constraint sets?
  // for capman, merge first

  //checkForImpliedUpdates();
  if (valid_ == false || csm.valid_ == false) {
    DEBUG(llvm::cout << "Adding constraints failed in (step 0): ");
    DEBUG(llvm::cout << "already invalid: " << valid_ << ", " << csm.valid_ << "\n");
    valid_ = false;
    return valid_;
  }

  /*
  if (intersects(csm.implied_updates_)) {
    applyUpdates(csm.implied_updates_);
    if (!valid_) { 
      DEBUG(llvm::cout << "Adding constraints failed in (step 1).\n");
      return valid_;
    }
  }

  // if we don't intersect with implied updates, no need to duplicate csm
  if (csm.intersects(implied_updates_)) {
    ConstraintSetFamily csm_updated(csm); 
    csm_updated.applyUpdates(implied_updates_);
    if (!csm_updated.valid_) {
      DEBUG(llvm::cout << "Adding constraints failed in (step 2).\n");
      valid_ = false;
      return valid_;
    }
    merge(csm_updated);
    //constraint_sets_union_.add(csm_updated.constraint_sets_union_);
  } else {
    merge(csm);
    //constraint_sets_union_.add(csm.constraint_sets_union_);
  }
  */

  merge(csm);

  // check if any new implied updates can be found after the merge
  if (valid_)  {
    //applyUpdates(implied_updates_);
    applyImpliedUpdates();
    if (valid_ && checkForImpliedUpdates()) {
      //applyUpdates(implied_updates_);
      applyImpliedUpdates();
      if (!valid_)
        DEBUG(llvm::cout << "Adding constraints failed in (step 3).\n");
    }
  } else {
    DEBUG(llvm::cout << "Adding constraints failed in (step 4).\n");
  }

  //checkForImpliedUpdates();
  return valid_;
}

void ConstraintSetFamily::merge(const ConstraintSetFamily &csm) {
  // merge constraint_sets
  foreach(it, csm.constraint_sets_.begin(), csm.constraint_sets_.end()) {
    bool constraint_added = false;
    foreach(it2, constraint_sets_.begin(), constraint_sets_.end()) {
      if ((constraint_added == false) && (*it).intersects(*it2)) {
        (*it2).add(*it);
        constraint_sets_union_.add(*it);
        constraint_added = true;
        break;
      }
    }
    // didn't intersect 
    if (constraint_added == false) {
      constraint_sets_.push_back(*it);
    }
  }

  // merge implied_updates
  foreach(it, csm.implied_updates_.begin(), csm.implied_updates_.end()) {
    shared_ptr<MemoryUpdate> mu = implied_updates_.find(it->first);
    if (mu.get()) {
      if (mu->testForConflict(*(it->second))) {
        DEBUG(llvm::cout << "Conflicting Implied Updates.\n");
        valid_ = false;
        return;
      }
      foreach(it2, it->second->begin(), it->second->end()) {
        mu->add(it2->first, it2->second);
      }
    } else {
      implied_updates_.add_new(it->second);
    }
  }

  MemoryUpdateSet new_implied_updates;
  int count_add = 0;
  int count_remove = 0;
  foreach(it, implied_updates_.begin(), implied_updates_.end()) {
    if (constraint_sets_union_.intersects(MemoryUpdateSet(it->second))) {
      new_implied_updates.add_new(it->second);
      count_add++;
    } else {
      count_remove++;
    }
  }
  //llvm::cout<< "MERGE: impliedupdates - added " << count_add << ", removed "
  //  << count_remove << ".\n";
  implied_updates_ = new_implied_updates;
}

#if 0
int ConstraintSetFamily::removeNonMarkedConstraints(std::ostream &os) {
  int removed_count = 0;
  std::vector<ConstraintSet> cs_vec;
  ConstraintSet cs_union(ConstantExpr::create(1, Expr::Bool), false);
  foreach(cs, constraint_sets_.begin(), constraint_sets_.end()) {
    if ((*cs).get_mark()) {
      cs_vec.push_back(*cs);
      cs_union.add(*cs);
#ifndef NDEBUG
			if (DebugFlag && isCurrentDebugType(DEBUG_TYPE)) {
				os << "Keeping marked constraint: " << *cs << " =\n";
				foreach(it, (*cs).begin(), (*cs).end()) {
					if (it != (*cs).begin()) os << ",\n";
					os << *it;
				}
				os << "\n";
			}
#endif

    } else {
      removed_count++;
#ifndef NDEBUG
			if (DebugFlag && isCurrentDebugType(DEBUG_TYPE)) {
				os << "Removing non-marked constraint: " << *cs << " =\n";
				foreach(it, (*cs).begin(), (*cs).end()) {
					if (it != (*cs).begin()) os << ",\n";
					os << *it;
				}
				os << "\n";
			}
#endif
    }
  }
  constraint_sets_union_ = cs_union;
  constraint_sets_.swap(cs_vec);
  return removed_count;
}
#endif

#if 1
int ConstraintSetFamily::removeNonMarkedConstraints(
		std::set< std::string > marked_array_names, std::ostream &os) {
	  int removed_count = 0;
  std::vector<ConstraintSet> cs_vec;
  ConstraintSet cs_union(ConstantExpr::create(1, Expr::Bool), false);
  foreach(cs, constraint_sets_.begin(), constraint_sets_.end()) {
    if ((*cs).get_mark()) {
      cs_vec.push_back(*cs);
      cs_union.add(*cs);
#ifndef NDEBUG
			if (DebugFlag && isCurrentDebugType(DEBUG_TYPE)) {
				os << "Keeping marked constraint: " << *cs << " =\n";
				foreach(it, (*cs).begin(), (*cs).end()) {
					if (it != (*cs).begin()) os << ",\n";
					os << *it;
				}
				os << "\n";
			}
#endif

    } else {
      removed_count++;
#ifndef NDEBUG
			if (DebugFlag && isCurrentDebugType(DEBUG_TYPE)) {
				os << "Removing non-marked constraint: " << *cs << " =\n";
				foreach(it, (*cs).begin(), (*cs).end()) {
					if (it != (*cs).begin()) os << ",\n";
					os << *it;
				}
				os << "\n";
			}
#endif
    }
  }
  constraint_sets_union_ = cs_union;
  constraint_sets_.swap(cs_vec);
  return removed_count;
}
#endif

#if 0
int ConstraintSetFamily::removeNonMarkedConstraints(
		std::set< std::string > marked_array_names, std::ostream &os) {
  int removed_count = 0;
  std::vector<ConstraintSet> cs_vec;
  ConstraintSet cs_union(ConstantExpr::create(1, Expr::Bool), false);
	//foreach(array_name, marked_array_names.begin(), marked_array_names.end()) {
	//	os << "Marked array: " << *array_name << "\n";
	//}
	foreach(mu, implied_updates_.begin(), implied_updates_.end()) {
		os << "Implied update array: " << mu->first << "\n";
	}
  foreach(cs, constraint_sets_.begin(), constraint_sets_.end()) {
		std::vector<const Array*> arrays = (*cs).arrays();
		bool is_marked = false;
		foreach(array, arrays.begin(), arrays.end()) {
			if (marked_array_names.count((*array)->name)) {
				os << "Array: " << (*array)->name << " is marked.\n";
				is_marked = true;
				break;
			} else {
				os << "Array: " << (*array)->name << " is not marked.\n";
			}
		}
    if (is_marked) {
      cs_vec.push_back(*cs);
      cs_union.add(*cs);
#ifndef NDEBUG
			if (DebugFlag && isCurrentDebugType(DEBUG_TYPE)) {
				os << "Keeping marked constraint: " << *cs << " =\n";
				foreach(it, (*cs).begin(), (*cs).end()) {
					if (it != (*cs).begin()) os << ",\n";
					os << *it;
				}
				os << "\n";
			}
#endif

    } else {
      removed_count++;
#ifndef NDEBUG
			if (DebugFlag && isCurrentDebugType(DEBUG_TYPE)) {
				os << "Removing non-marked constraint: " << *cs << " =\n";
				foreach(it, (*cs).begin(), (*cs).end()) {
					if (it != (*cs).begin()) os << ",\n";
					os << *it;
				}
				os << "\n";
			}
#endif
    }
  }
  constraint_sets_union_ = cs_union;
  constraint_sets_.swap(cs_vec);

  //MemoryUpdateSet new_implied_updates;
  //foreach(it, implied_updates_.begin(), implied_updates_.end()) {

	//	if (constraint_sets_union_.intersects(MemoryUpdateSet(it->second))
	//			|| marked_array_names.count(it->first)) {
  //  	new_implied_updates.add_new(it->second);
	//	}
  //}
  //implied_updates_ = new_implied_updates;
  return removed_count;
}
#endif

//bool ConstraintSetFamily::pruneImpliedUpdates(const MemoryUpdateSet &us) {
//  MemoryUpdateSet new_implied_updates;
//  // only checks memory object id intersection, could be finer grained and
//  // check index intersection
//  foreach(it, implied_updates_.begin(), implied_updates_.end()) {
//    if (us.find(it->first) != NULL) {
//      new_implied_updates.add_new(it->second);
//    }
//  }
//  implied_updates_ = new_implied_updates;
//  return true;
//}

bool ConstraintSetFamily::equals(const ConstraintSetFamily &b) const {
  bool match = true;
  if (implied_updates_.equals(b.implied_updates_)) {
    if (constraint_sets_.size() != b.constraint_sets_.size()) {
      match = false;
    } else {
      match = true;
      foreach(cs, constraint_sets_.begin(), constraint_sets_.end()) {
        match = false;
        foreach(b_cs, b.constraint_sets_.begin(), b.constraint_sets_.end()) {
          match = (*cs).equals(*b_cs);
          if (match) break;
        }
        if (!match) return false;
      }
    }
  } else {
    match = false;
  }
  return match;
}
//===----------------------------------------------------------------------===//
