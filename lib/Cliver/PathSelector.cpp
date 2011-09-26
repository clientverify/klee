//===-- PathSelector.cpp -----------------------------------------*- C++ -*-===//
//
// <insert license>
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//

#include "PathSelector.h"
#include "Path.h"
#include "PathManager.h"
#include "CVCommon.h"

////////////////////////////////////////////////////////////////////////////////

namespace cliver {

PathSelector::PathSelector() : index_(0) {}

PathSelector::PathSelector(PathManagerSet *paths) : index_(0) {
	//// XXX fixme
	//foreach(PathManager* path, paths) {
	//	paths_.push_back(path);
	//}
}

////////////////////////////////////////////////////////////////////////////////

PathSelector* PathSelectorFactory::create(PathManagerSet* paths) {
  switch (g_cliver_mode) {
		case VerifyWithTrainingPaths: 
			return new PathSelector(paths);
		case DefaultMode:
		case DefaultTrainingMode:
		default:
			break;
  }
	cv_error("PathSelector only used in verification modes.");
}

} // end namespace cliver
