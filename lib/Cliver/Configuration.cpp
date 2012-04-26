//===-- Configuration.cpp----------------------------------------*- C++ -*-===//
//
// <insert license>
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//

#include "cliver/ClientVerifier.h"
#include "cliver/CVSearcher.h"
#include "cliver/CVStream.h"
#include "cliver/ExecutionStateProperty.h"
#include "cliver/ExecutionTree.h"
#include "cliver/NetworkManager.h"
#include "CVCommon.h"

#include "llvm/Support/CommandLine.h"

namespace cliver {

////////////////////////////////////////////////////////////////////////////////

enum RunModeType {
  Verify,
  VerifyWithEditCost,
  Training,
  TestTraining
};

enum ClientModelType {
  Tetrinet,
  XPilot,
};

enum SearchModeType {
  Random,
  PriorityQueue,
  DepthFirst,
  BreadthFirst
};

////////////////////////////////////////////////////////////////////////////////

llvm::cl::opt<RunModeType> RunMode("cliver-mode", 
  llvm::cl::ValueRequired,
  llvm::cl::desc("Mode in which cliver should run"),
  llvm::cl::values(
    clEnumValN(Verify, "verify", "Verify mode"),
    clEnumValN(VerifyWithEditCost, "verify-with-edit-cost",
      "Verify using edit cost and training data"),
    clEnumValN(Training, "training", "Generate training traces"),
  clEnumValEnd));

llvm::cl::opt<ClientModelType> ClientModel("client-model",
  llvm::cl::ValueRequired,
  llvm::cl::desc("Model used for client"),
  llvm::cl::values(
    clEnumValN(Tetrinet, "tetrinet", "Tetrinet"),
    clEnumValN(XPilot,   "xpilot",   "XPilot"),
  clEnumValEnd));

llvm::cl::opt<SearchModeType> SearchMode("search-mode",
  llvm::cl::desc("Manner in which states are selected for execution"),
  llvm::cl::values(
    clEnumValN(Random,        "random", "Random mode"),
    clEnumValN(PriorityQueue, "pq",     "Priority queue mode"),
    clEnumValN(BreadthFirst,  "bfs",    "Breadth first mode"),
    clEnumValN(DepthFirst,    "dfs",    "Depth first mode"),
  clEnumValEnd),
  llvm::cl::init(DepthFirst));

////////////////////////////////////////////////////////////////////////////////

CVSearcher* CVSearcherFactory::create(klee::Searcher* base_searcher, 
                                      ClientVerifier* cv, StateMerger* merger) {
  switch (RunMode) {
    case Verify:
    case VerifyWithEditCost: {
      switch (ClientModel) {
        case Tetrinet: {
          return new VerifySearcher(cv, merger);
        }
        case XPilot: {
          return new MergeVerifySearcher(cv, merger);
        }
      }
    }
    case Training: {
      return new TrainingSearcher(cv, merger);
    }
  }
  cv_error("run mode not supported!");
  return NULL;
}

////////////////////////////////////////////////////////////////////////////////

SearcherStage* SearcherStageFactory::create(StateMerger* merger, 
                                            CVExecutionState* state) {
  switch (SearchMode) {
    case Random: {
      return new RandomSearcherStage(state);
    }
    case PriorityQueue: {
      return new PQSearcherStage(state);
    }
    case BreadthFirst: {
      return new BFSSearcherStage(state);
    }
    case DepthFirst: {
      return new DFSSearcherStage(state);
    }
  }
  cv_error("search mode not supported!");
  return NULL;
}

////////////////////////////////////////////////////////////////////////////////

NetworkManager* NetworkManagerFactory::create(CVExecutionState* state,
                                              ClientVerifier* cv) {
  switch (ClientModel) {
    case Tetrinet: {
      NetworkManager *nm = new NetworkManager(state);
      foreach( SocketEventList *sel, cv->socket_events()) {
        nm->add_socket(*sel);
      }
      return nm;
    }
    case XPilot: {
      NetworkManagerXpilot *nm = new NetworkManagerXpilot(state);
      foreach( SocketEventList *sel, cv->socket_events()) {
        nm->add_socket(*sel);
      }
      return nm;
    }
  }
  cv_error("network manager mode not supported!");
  return NULL;
}

////////////////////////////////////////////////////////////////////////////////

ExecutionTreeManager* ExecutionTreeManagerFactory::create(ClientVerifier* cv) {
  switch (RunMode) {

    case Verify: {
      return new ExecutionTreeManager(cv);
      break;
    }

    case VerifyWithEditCost: {
      if (SearchMode != PriorityQueue)
        SearchMode = PriorityQueue;
      return new VerifyExecutionTreeManager(cv);
      break;
    }

    case Training: {
      return new TrainingExecutionTreeManager(cv);
    }

  }
  cv_message("cliver mode not supported in ExecutionTreeManager");
  return NULL;
}

////////////////////////////////////////////////////////////////////////////////

ExecutionStateProperty* ExecutionStatePropertyFactory::create() {
  switch (RunMode) {

    case Training:
    case Verify:
      return new VerifyProperty();

    case VerifyWithEditCost:
      return new EditDistanceProperty();
  }
  cv_error("invalid run mode");
  return NULL;
}

////////////////////////////////////////////////////////////////////////////////

} // end namespace cliver

