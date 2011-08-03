//===-- PathManager.cpp -----------------------------------------*- C++ -*-===//
//
// <insert license>
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//

#include "PathManager.h"
#include "llvm/Support/CommandLine.h"

namespace cliver {

////////////////////////////////////////////////////////////////////////////////

enum PathModel {
  DefaultPathModel
};

llvm::cl::opt<PathModel>
cl_path_model("path-model", 
  llvm::cl::desc("Choose the pathmodel."),
  llvm::cl::values(
    clEnumValN(DefaultPathModel, "default", 
      "Default network model"),
  clEnumValEnd),
  llvm::cl::init(DefaultPathModel));

////////////////////////////////////////////////////////////////////////////////

PathManager::PathManager() {}

PathManager* PathManager::clone() {
	return new PathManager(*this);
}

void PathManager::add_false_branch(klee::KInstruction* inst) {
	add_branch(false, inst);
}

void PathManager::add_true_branch(klee::KInstruction* inst) {
	add_branch(true, inst);
}

void PathManager::add_branch(bool direction, klee::KInstruction* inst) {
	branches_.push_back(BranchEvent(direction, inst));
}

PathManager* PathManagerFactory::create() {
  switch (cl_path_model) {
	case DefaultPathModel: 
    break;
  }
  return new PathManager();
}


////////////////////////////////////////////////////////////////////////////////

} // end namespace cliver
