//===-- LazyConstraintConfig.h ---------------------------------*- C++ -*-===//
//
// <insert license>
//
//===---------------------------------------------------------------------===//
//
// LazyConstraint Configuration File
//
//===---------------------------------------------------------------------===//
#ifndef LIB_CLIVER_LAZYCONSTRAINTCONFIG_H_
#define LIB_CLIVER_LAZYCONSTRAINTCONFIG_H_

#include <map>
#include <string>

#include "cliver/LazyConstraint.h"

namespace cliver {

/// \brief Database of functions that can be used as the trigger function
/// for LazyConstraints (singleton)
class LazyTriggerFuncDB {
public:

  /// \brief Access singleton instance.
  static LazyTriggerFuncDB &Instance() {
    static LazyTriggerFuncDB *p = new LazyTriggerFuncDB();

    // Preload some testing-only trigger functions into the database. If for
    // some reason later on, we don't want test functions in the database, we
    // could introduce a command-line option that enables/disables these.
    static bool preloaded = false;
    if (!preloaded) {
      preloaded = true;
      p->preload();
    }

    return *p;
  }

  /// \brief Preload trigger functions from config code
  void preload();

  size_t size() const { return db_.size(); }

  void insert(const std::string &name, LazyConstraint::TriggerFunc f) {
    db_[name] = f;
  }

  /// \brief Find the lazy constraint trigger function by name.
  /// \return function pointer or NULL if not in the database
  LazyConstraint::TriggerFunc find(const std::string &name) const {
    auto it = db_.find(name);
    if (it == db_.end()) {
      return NULL;
    } else {
      return it->second;
    }
  }

private:
  LazyTriggerFuncDB() {} // forbid constructor (private so it cannot be called)
  LazyTriggerFuncDB(LazyTriggerFuncDB const &); // forbid copy constructor
  LazyTriggerFuncDB &operator=(LazyTriggerFuncDB const &); // forbid assign

  std::map<std::string, LazyConstraint::TriggerFunc> db_;
};

}  // End cliver namespace

#endif  // LIB_CLIVER_LAZYCONSTRAINTCONFIG_H_
