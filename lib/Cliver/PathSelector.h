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

class PathManagerSet;
class PathManager;

class PathSelector {
 public:
	PathSelector();
	PathSelector(PathManagerSet* paths);
	PathManager* next_path();
 protected: 
	unsigned index_;
	std::vector<PathManager*> paths_;
};

class PathSelectorFactory {
 public:
  static PathSelector* create(PathManagerSet* paths);
};

} // end namespace cliver
#endif // CLIVER_PATH_SELECTOR_H
