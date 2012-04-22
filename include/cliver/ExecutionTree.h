//===-- ExecutionTree.h -----------------------------------------*- C++ -*-===//
//
// <insert license>
//
//===----------------------------------------------------------------------===//
//
// TODO Rename this file to ExecutionTraceManager.h
//
//===----------------------------------------------------------------------===//
#ifndef CLIVER_EXECUTION_TREE_H
#define CLIVER_EXECUTION_TREE_H

#include "cliver/EditDistance.h"
#include "cliver/ExecutionStateProperty.h"
#include "cliver/ExecutionObserver.h"
#include "cliver/ExecutionTrace.h"
#include "cliver/TrackingRadixTree.h"
#include "cliver/LevenshteinRadixTree.h"
#include "cliver/Training.h"

#include "cliver/tree.h"

#include "klee/Solver.h"
#include "klee/Internal/Module/KModule.h"

#include "llvm/Analysis/Trace.h"

#include <set>
#include <list>
#include <map>
#include <iostream>
#include <vector>

#include <boost/shared_ptr.hpp>
#include <boost/serialization/access.hpp>
#include <boost/serialization/vector.hpp>
#include <boost/serialization/set.hpp>

namespace cliver {

////////////////////////////////////////////////////////////////////////////////

typedef TrackingRadixTree< ExecutionTrace, BasicBlockID, CVExecutionState > 
    ExecutionTraceTree;

typedef LevenshteinRadixTree< ExecutionTrace, BasicBlockID > 
    EditDistanceExecutionTree;

typedef boost::unordered_map< CVExecutionState*, EditDistanceExecutionTree* >
    EditDistanceExecutionTreeMap;

////////////////////////////////////////////////////////////////////////////////

/// TODO: Rename this class to ExecutionTraceManager
class ExecutionTreeManager : public ExecutionObserver {
 public:
  ExecutionTreeManager(ClientVerifier *cv);
  virtual void initialize();
  virtual void notify(ExecutionEvent ev);
 protected:
  std::vector< ExecutionTraceTree* > tree_list_;
  ClientVerifier *cv_;

};

class TrainingExecutionTreeManager : public ExecutionTreeManager {
 public:
  TrainingExecutionTreeManager(ClientVerifier *cv);
  void initialize();
  void notify(ExecutionEvent ev);
 protected:

};

class VerifyExecutionTreeManager : public ExecutionTreeManager {
 public:
  VerifyExecutionTreeManager(ClientVerifier *cv);
  virtual void initialize();
  virtual void notify(ExecutionEvent ev);

 private:
  void clear_edit_distance_map();

  TrainingObjectSet training_data_;
  EditDistanceExecutionTreeMap edit_distance_map_;
  EditDistanceExecutionTree *root_tree_;

};

////////////////////////////////////////////////////////////////////////////////

class ExecutionTreeManagerFactory {
 public:
  static ExecutionTreeManager* create(ClientVerifier *cv);
};


} // end namespace cliver

#endif // CLIVER_EXECUTION_TREE_H

