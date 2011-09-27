//===-- PathSelector.h ------------------------------------------*- C++ -*-===//
//
// <insert license>
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//
#ifndef CLIVER_PATH_SELECTOR_H
#define CLIVER_PATH_SELECTOR_H

#include <set>
#include <vector>

////////////////////////////////////////////////////////////////////////////////

namespace cliver {

class PathManager;
class PathManagerSet;

class PathSelector {
 public:
	virtual PathManager* next_path() = 0;
	virtual PathSelector* clone() = 0;
};

class OrderedSetPathSelector : public PathSelector {
 public:
	OrderedSetPathSelector();
	virtual PathManager* next_path();
	virtual PathSelector* clone();

 protected: 
	unsigned index_;
};

class PathSelectorFactory {
 public:
  static PathSelector* create(PathManagerSet* training_paths);
};

} // end namespace cliver
#endif // CLIVER_PATH_SELECTOR_H
