//===-- CVAssignment.cpp ----------------------------------------*- C++ -*-===//
//
// <insert license>
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//

#ifndef CV_ASSIGNMENT_H
#define CV_ASSIGNMENT_H

#include "klee/util/Assignment.h"
#include "klee/Solver.h"

#include <ostream>

namespace cliver {
class CVAssignment : public klee::Assignment {
 public:
  typedef std::map<std::string, const klee::Array*> name_ty;

  name_ty name_bindings;

  CVAssignment() : klee::Assignment(false) {}

  CVAssignment(klee::ref<klee::Expr> &e);

  CVAssignment(std::vector<const klee::Array*> &objects, 
               std::vector< std::vector<unsigned char> > &values);

  void solveForBindings(klee::Solver* solver, 
                        klee::ref<klee::Expr> &expr);

  void addBindings(std::vector<const klee::Array*> &objects, 
                   std::vector< std::vector<unsigned char> > &values);

  const klee::Array* getArray(std::string &name) {
    if (name_bindings.find(name) != name_bindings.end()) {
      return name_bindings[name];
    }
    return NULL;
  }

  std::vector<unsigned char>* getBindings(std::string &name) {
    if (name_bindings.find(name) != name_bindings.end()) {
      return &(bindings[name_bindings[name]]);
    }
    return NULL;
  }

  bool has(std::string &name) {
    return name_bindings.find(name) != name_bindings.end();
  }

  void clear() {
    bindings.clear();
    name_bindings.clear();
  }

  size_t size() {
    return name_bindings.size();
  }

  void print(std::ostream& os) const;

};

////////////////////////////////////////////////////////////////////////////////

std::ostream &operator<<(std::ostream &os, const CVAssignment &x);

////////////////////////////////////////////////////////////////////////////////

} // End cliver namespace

#endif


