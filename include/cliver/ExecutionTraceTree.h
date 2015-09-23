//===-- ExecutionTraceTree.h ------------------------------------*- C++ -*-===//
//
// <insert license>
//
//===----------------------------------------------------------------------===//
//
//===----------------------------------------------------------------------===//
#ifndef CLIVER_EXECUTION_TRACE_TREE_H
#define CLIVER_EXECUTION_TRACE_TREE_H

#include "cliver/ExecutionTrace.h" // Defines ExecutionTrace, BasicBlockID
#include "cliver/TrackingRadixTreeExt.h"
#include "cliver/EditDistanceTree.h"

namespace cliver {

class ExecutionStateProperty;

typedef TrackingRadixTreeExt< ExecutionTrace, 
                           BasicBlockID, 
                           ExecutionStateProperty > ExecutionTraceTree;

typedef TrackingRadixTreeExt< ExecutionTrace, 
                              BasicBlockID, 
                              ExecutionStateProperty >::Node ExecutionTraceTreeNode;

typedef EditDistanceTree< ExecutionTrace, 
                          BasicBlockID > ExecutionTraceEditDistanceTree;

} // end namespace cliver

#endif
