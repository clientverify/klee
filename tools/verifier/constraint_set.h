#ifndef CONSTRAINT_SET_H
#define CONSTRAINT_SET_H

#include "memory_update.h"
#include "frame_stats.h"

#include "klee/Core/ImpliedValue.h"
#include "klee/util/ExprVisitor.h"
#include "klee/util/ExprEvaluator.h"
#include "klee/util/ExprPPrinter.h"
#include "klee/util/ExprUtil.h"
#include "klee/util/ExprHashMap.h"
//#include "klee/lib/util/IntEvaluation.h"

#include <vector>
#include <string>
#include <map>
#include <list>
#include <iostream>

//===----------------------------------------------------------------------===//
// Some code in this file and constraint_set.cpp is taken from 
// klee/lib/opt/IndependentSolver.cpp, which has some classes and funcs 
// that aren't in a header file (DenseSet).

//===----------------------------------------------------------------------===//

namespace klee {

template<class T>
ref<Expr> vecExpr( T begin, T end, Expr::Kind kind) {
  ref<Expr> res;
  foreach(it, begin, end) {
    if (res.get()) {
      switch(kind) {
      case Expr::And:
        res = AndExpr::create(res, *it);
        break;
      case Expr::Or:
        res = OrExpr::create(res, *it);
        break;
      default:
        assert(false && "invalid type");
      }
    } else {
      res = *it;
    }
  }
  return res;
}

//===----------------------------------------------------------------------===//
// ConstraintGenerator is a visitor class which rewrites ReadExprs, of
// Arrays for which the client has provided updates, with ConstantExprs.

template<class T>
class ConstraintGenerator: public ExprEvaluator {
public:
  ConstraintGenerator(T* pre_updates, T* post_updates= NULL) 
    : PreUpdates(pre_updates), PostUpdates(post_updates) {}

  T *PreUpdates;
  T *PostUpdates;

protected:
  ref<Expr> getInitialValue(const Array &array, unsigned index) {
    using namespace std;
    shared_ptr<MemoryUpdate> Update, PreUpdate, PostUpdate;

    PreUpdate = PreUpdates->find(array.name);

    if (PostUpdates)
      PostUpdate = PostUpdates->find(array.name);
      
    assert(!PreUpdate.get() || !PostUpdate.get() 
           && "multiple MemoryUpdate matches");

    if (!PostUpdate.get())
      Update = PreUpdate;
    else
      Update = PostUpdate;
        
    uint8_t value;
    if (Update.get() && Update->find(index, value)) {
      //llvm::cerr << "array: " << array.name 
      //  << "[" << index << "] = " << value << "\n";
      return ConstantExpr::create(value, Expr::Int8);
    } else {
      //llvm::cerr << "array: " << array.name 
      //  << "[" << index << "] = symbolic \n";
      Array *array_ptr;
      if (Update)
        array_ptr = Update->array();
      else 
        array_ptr = const_cast<Array*>(&array);

      return ReadExpr::create(UpdateList(array_ptr, NULL), 
                              ConstantExpr::create(index, Expr::Int32));
    }
  }
};

//===----------------------------------------------------------------------===//

/*
class UpdateListFinder: public ExprVisitor {
public:
  UpdateListFinder() {}

  ExprVisitor::Action visitRead(const ReadExpr &re) {
    visit(re.index);
    if (re.updates.head != NULL) {
      if (update_set.insert(re.updates.name).second)
        names.push_back(re.updates.name);
      for (const UpdateNode *un=re.updates.head; un; un=un->next) {
        visit(un->index);
        visit(un->value);
      }
    }
    return Action::doChildren();
  }

  bool check(ref<Expr> e) {
    std::vector< ref<ReadExpr> >;
    findReads
  }

  std::vector< std::string > names;
  std::set<std::string> update_set;
};
*/

//===----------------------------------------------------------------------===//


#if 0
//===----------------------------------------------------------------------===//
// ConstraintGenerator is a visitor class which rewrites ReadExprs, of
// Arrays for which the client has provided updates, with ConstantExprs.

template<class T>
class ConstraintGenerator: public ExprEvaluator {
protected:
  ref<Expr> getInitialValue(const Array &array, unsigned index) {
    using namespace std;
    shared_ptr<MemoryUpdate> Update, PreUpdate, PostUpdate;

    PreUpdate = PreUpdates->find(array.name);

    if (PostUpdates)
      PostUpdate = PostUpdates->find(array.name);
      
    assert(!PreUpdate.get() || !PostUpdate.get() 
           && "multiple MemoryUpdate matches");

    if (!PostUpdate.get())
      Update = PreUpdate;
    else
      Update = PostUpdate;
        
    uint8_t value;
    if (Update.get() && Update->find(index, value)) {
      //llvm::cerr << "array: " << array.name 
      //  << "[" << index << "] = " << value << "\n";
      return ConstantExpr::create(value, Expr::Int8);
    } else {
      //llvm::cerr << "array: " << array.name 
      //  << "[" << index << "] = symbolic \n";
      Array *array_ptr;
      if (Update)
        array_ptr = Update->array();
      else 
        array_ptr = const_cast<Array*>(&array);

      return ReadExpr::create(UpdateList(array_ptr, NULL), 
                              ConstantExpr::create(index, Expr::Int32));
    }
  }

public:
  T *PreUpdates;
  T *PostUpdates;

  ConstraintGenerator(T* pre_updates, T* post_updates= NULL) 
    : PreUpdates(pre_updates), PostUpdates(post_updates) {}
};
#endif

//===----------------------------------------------------------------------===//

template<class T>
class DenseSet {
  std::set<T> s;

public:
  DenseSet() {}

  void add(T x) {
    s.insert(x);
  }
  void add(T start, T end) {
    for (; start<end; start++)
      s.insert(start);
  }

  // returns true iff set is changed by addition
  bool add(const DenseSet &b) {
    bool modified = false;
    foreach(it, b.s.begin(), b.s.end()) {
      if (modified || !s.count(*it)) {
        modified = true;
        s.insert(*it);
      }
    }
    return modified;
  }

  bool intersects(const DenseSet &b) const {
    foreach(it, s.begin(), s.end())
      if (b.s.count(*it))
        return true;
    return false;
  }

  void print(std::ostream &os) const {
    bool first = true;
    os << "{";
    foreach(it, s.begin(), s.end()) {
      if (first) {
        first = false;
      } else {
        os << ",";
      }
      os << *it;
    }
    os << "}";
  }
};

template<class T>
inline std::ostream &operator<<(std::ostream &os, const DenseSet<T> &dis) {
  dis.print(os);
  return os;
}

//===----------------------------------------------------------------------===//
//
// ConstraintSet maintains a set of ref<Expr> constraints. If we form an 
// undirected graph where each ref<Expr> is a node and edges between nodes 
// represent shared memory reads, the graph will be connected. (This property is
// not enforced, and sometimes for convenience the constraints are unconnected,
// an example being constraint_sets_union in ConstraintSetFamily).
// The ref<Expr> are stored in the vector _constraints. The std::map _elements 
// maintains a record of the shared Arrays and associated indicies of 
// the ReadExprs that are in _constraints. _whole_objects maintains 
// the Arrays of the ReadExprs in _constraints with symbolic indicies, 
// the common case is for _whole_objects to be empty.

class ConstraintSet {
public:
  ConstraintSet();
  ConstraintSet(const ConstraintSet &cs);
  ConstraintSet(const MemoryUpdateSet &us);
  ConstraintSet(ref<Expr> e, bool store_constraints = true);
  ConstraintSet &operator=(const ConstraintSet &cs);

  bool intersects(const MemoryUpdateSet &us) const;
  bool intersects(const ConstraintSet &b) const; 
  bool add(const ConstraintSet &b);
  bool equals(const ConstraintSet &b) const;
  void print(std::ostream &os) const;

  void set_mark(bool m) { marked_ = m; }
  bool get_mark() { return marked_; }
  bool check_implied() { return check_implied_; }
  void set_check_implied() { check_implied_ = true; }
  size_t size() const { return constraints_.size(); }

#ifdef CV_USE_HASH
  typedef ExprHashSet ConstraintExprSet;
#else
  typedef std::vector<ref<Expr> > ConstraintExprSet;
#endif

  ConstraintExprSet::const_iterator begin() const { 
    return constraints_.begin();
  }
  ConstraintExprSet::const_iterator end() const { 
    return constraints_.end();
  }

  std::vector<const Array*> arrays() const {
    std::vector<const Array*> Objects;
    foreach (element, elements_.begin(), elements_.end()) {
      if (whole_objects_.count(element->first) == 0) {
        Objects.push_back(element->first);
      }
    }
    foreach (object, whole_objects_.begin(), whole_objects_.end()) {
      Objects.push_back(*object);
    }
    return Objects;
  }

private:
  // elements_ stores the indices of the read expressions of this ConstraintSet,
  // each Array that we read is mapped to its own index set.
  std::map<const Array*, DenseSet<unsigned> > elements_;

  // If an Expr in this ConstraintSet contains a symbolic read, i.e., the read
  // index is symbolic, this ConstraintSet must "own" the entire Array.
  std::set<const Array*> whole_objects_;

  // The Expr that this ConstraintSet represents.
  ConstraintExprSet constraints_;
  
  // If we want to check if a MemoryUpdateSet intersects with a ConstraintSet,
  // we create a ConstraintSet from the MemoryUpdateSet and therefore 
  // don't need to store any constraints. XXX: still neccessary?
  bool store_constraints_;
  bool marked_;
  bool check_implied_;
};

inline std::ostream &operator<<(std::ostream &os, const ConstraintSet &cs) {
  cs.print(os);
  return os;
}

//===----------------------------------------------------------------------===//

class ConstraintSetFamily {
public:
  ConstraintSetFamily() : valid_(true) {}
  ConstraintSetFamily(const std::vector< ref<Expr> > evec);
  ConstraintSetFamily(ref<Expr> e);

  void set_mark(bool m);
  bool applyImpliedUpdates();
  bool applyUpdates(const MemoryUpdateSet &us);
  bool checkForImpliedUpdates();
  bool add(const ConstraintSetFamily& csm);
  bool add(ref<Expr> e);
  bool intersects(const MemoryUpdateSet &us) const;
  const MemoryUpdateSet& getImpliedUpdates() const { return implied_updates_; }
  const ConstraintSet& getUnion() const { return constraint_sets_union_; }
  //bool pruneImpliedUpdates(const MemoryUpdateSet &us);
  //int removeNonMarkedConstraints(std::ostream &os);
	int removeNonMarkedConstraints(std::set<std::string> marked_array_names, std::ostream &os);

  bool valid() const { return valid_; } 
  void print(std::ostream &os) const;

  bool equals(const ConstraintSetFamily &b) const;

  typedef std::vector<ConstraintSet >::const_iterator constraint_sets_iterator;
  constraint_sets_iterator begin() const { return constraint_sets_.begin(); }
  constraint_sets_iterator end() const { return constraint_sets_.end(); }

private:
  void merge(const ConstraintSetFamily &csm);
  void addConstraintInternal(ref<Expr> _e, bool marked);

  void getImpliedValues(ref<Expr> e,
                        ref<ConstantExpr> value,
                        ImpliedValueList &results);
private:
  bool valid_;
  MemoryUpdateSet            implied_updates_;
  std::vector<ConstraintSet> constraint_sets_;
  ConstraintSet              constraint_sets_union_;
};

inline std::ostream &operator<<(std::ostream &os, 
                                const ConstraintSetFamily &csm) {
  csm.print(os);
  return os;
}

//===----------------------------------------------------------------------===//

} // end namespace klee

#endif // CONSTRAINT_SET_H
