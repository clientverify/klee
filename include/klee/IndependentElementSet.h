//===-- IndependentElementSet.h ---------------------------------*- C++ -*-===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef __UTIL_INDEPENDENTELEMENT_SET_H__
#define __UTIL_INDEPENDENTELEMENT_SET_H__

#include "klee/Expr.h"
#include "klee/util/ExprUtil.h"

#include <map>
#include <vector>
#include <ostream>

namespace klee {
template<class T>
class DenseSet {
  typedef std::set<T> set_ty;
  set_ty s;

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
    for (typename set_ty::const_iterator it = b.s.begin(), ie = b.s.end(); 
         it != ie; ++it) {
      if (modified || !s.count(*it)) {
        modified = true;
        s.insert(*it);
      }
    }
    return modified;
  }

  bool intersects(const DenseSet &b) {
    for (typename set_ty::iterator it = s.begin(), ie = s.end(); 
         it != ie; ++it)
      if (b.s.count(*it))
        return true;
    return false;
  }

  void print(std::ostream &os) const {
    bool first = true;
    os << "{";
    for (typename set_ty::iterator it = s.begin(), ie = s.end(); 
         it != ie; ++it) {
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

class IndependentElementSet {
  typedef std::map<const Array*, DenseSet<unsigned> > elements_ty;
  elements_ty elements;
  std::set<const Array*> wholeObjects;

public:
  IndependentElementSet() {}
  IndependentElementSet(ref<Expr> e) {
    std::vector< ref<ReadExpr> > reads;
    findReads(e, /* visitUpdates= */ true, reads);
    for (unsigned i = 0; i != reads.size(); ++i) {
      ReadExpr *re = reads[i].get();
      const Array *array = re->updates.root;
      
      // Reads of a constant array don't alias.
      if (re->updates.root->isConstantArray() &&
          !re->updates.head)
        continue;

      if (!wholeObjects.count(array)) {
        if (ConstantExpr *CE = dyn_cast<ConstantExpr>(re->index)) {
          DenseSet<unsigned> &dis = elements[array];
          dis.add((unsigned) CE->getZExtValue(32));
        } else {
          elements_ty::iterator it2 = elements.find(array);
          if (it2!=elements.end())
            elements.erase(it2);
          wholeObjects.insert(array);
        }
      }
    }
  }
  IndependentElementSet(const IndependentElementSet &ies) : 
    elements(ies.elements),
    wholeObjects(ies.wholeObjects) {}    

  IndependentElementSet &operator=(const IndependentElementSet &ies) {
    elements = ies.elements;
    wholeObjects = ies.wholeObjects;
    return *this;
  }

  void print(std::ostream &os) const {
    os << "{";
    bool first = true;
    for (std::set<const Array*>::iterator it = wholeObjects.begin(), 
           ie = wholeObjects.end(); it != ie; ++it) {
      const Array *array = *it;

      if (first) {
        first = false;
      } else {
        os << ", ";
      }

      os << "MO" << array->name;
    }
    for (elements_ty::const_iterator it = elements.begin(), ie = elements.end();
         it != ie; ++it) {
      const Array *array = it->first;
      const DenseSet<unsigned> &dis = it->second;

      if (first) {
        first = false;
      } else {
        os << ", ";
      }

      os << "MO" << array->name << " : " << dis;
    }
    os << "}";
  }

  // more efficient when this is the smaller set
  bool intersects(const IndependentElementSet &b) {
    for (std::set<const Array*>::iterator it = wholeObjects.begin(), 
           ie = wholeObjects.end(); it != ie; ++it) {
      const Array *array = *it;
      if (b.wholeObjects.count(array) || 
          b.elements.find(array) != b.elements.end())
        return true;
    }
    for (elements_ty::iterator it = elements.begin(), ie = elements.end();
         it != ie; ++it) {
      const Array *array = it->first;
      if (b.wholeObjects.count(array))
        return true;
      elements_ty::const_iterator it2 = b.elements.find(array);
      if (it2 != b.elements.end()) {
        if (it->second.intersects(it2->second))
          return true;
      }
    }
    return false;
  }

  // returns true iff set is changed by addition
  bool add(const IndependentElementSet &b) {
    bool modified = false;
    for (std::set<const Array*>::const_iterator it = b.wholeObjects.begin(), 
           ie = b.wholeObjects.end(); it != ie; ++it) {
      const Array *array = *it;
      elements_ty::iterator it2 = elements.find(array);
      if (it2!=elements.end()) {
        modified = true;
        elements.erase(it2);
        wholeObjects.insert(array);
      } else {
        if (!wholeObjects.count(array)) {
          modified = true;
          wholeObjects.insert(array);
        }
      }
    }
    for (elements_ty::const_iterator it = b.elements.begin(), 
           ie = b.elements.end(); it != ie; ++it) {
      const Array *array = it->first;
      if (!wholeObjects.count(array)) {
        elements_ty::iterator it2 = elements.find(array);
        if (it2==elements.end()) {
          modified = true;
          elements.insert(*it);
        } else {
          if (it2->second.add(it->second))
            modified = true;
        }
      }
    }
    return modified;
  }
};

inline std::ostream &operator<<(std::ostream &os, const IndependentElementSet &ies) {
  ies.print(os);
  return os;
}

}

#endif
