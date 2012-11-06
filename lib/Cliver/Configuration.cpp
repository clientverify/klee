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
#include "cliver/EditDistanceTree.h"
#include "cliver/EditDistanceTreeTest.h"
#include "cliver/ExecutionStateProperty.h"
#include "cliver/ExecutionTrace.h"
#include "cliver/ExecutionTraceManager.h"
#include "cliver/NetworkManager.h"
#include "cliver/SocketEventMeasurement.h"
#include "CVCommon.h"

#include "llvm/Support/CommandLine.h"

namespace cliver {

////////////////////////////////////////////////////////////////////////////////

llvm::cl::opt<RunModeType> RunMode("cliver-mode", 
  llvm::cl::ValueRequired,
  llvm::cl::desc("Mode in which cliver should run"),
  llvm::cl::values(
    clEnumValN(Training, "training", "Generate training traces"),
    clEnumValN(VerifyNaive, "naive", "Verify mode"),
    clEnumValN(VerifyEditDistanceRow, "edit-dist-row",
      "Verify using edit distance and training data with Lev. matrix row alg."),
    clEnumValN(VerifyEditDistanceKPrefixRow, "edit-dist-kprefix-row",
      "Verify using edit distance and training data with k-Prefix matrix row alg."),
    clEnumValN(VerifyEditDistanceKPrefixHash, "edit-dist-kprefix-hash",
      "Verify using edit distance and training data with k-Prefix hash table alg."),
    clEnumValN(VerifyEditDistanceKPrefixHashPointer, "edit-dist-kprefix-hashptr",
      "Verify using edit distance and training data with k-Prefix ptr hash table alg."),
    clEnumValN(VerifyEditDistanceKPrefixTest, "edit-dist-kprefix-test",
      "Verify using all k-Prefix to compare implementations."),
  clEnumValEnd));

ClientModelType ClientModelFlag;
llvm::cl::opt<ClientModelType,true> ClientModel("client-model",
  llvm::cl::location(ClientModelFlag),
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
  llvm::cl::init(PriorityQueue));

////////////////////////////////////////////////////////////////////////////////

CVSearcher* CVSearcherFactory::create(klee::Searcher* base_searcher, 
                                      ClientVerifier* cv, StateMerger* merger) {
  switch (RunMode) {
    case VerifyNaive:
    case VerifyEditDistanceRow:
    case VerifyEditDistanceKPrefixRow:
    case VerifyEditDistanceKPrefixHash:
    case VerifyEditDistanceKPrefixTest:
    case VerifyEditDistanceKPrefixHashPointer: {
      return new VerifySearcher(cv, merger);
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
  switch (RunMode) {
    case VerifyEditDistanceRow:
    case VerifyEditDistanceKPrefixRow:
    case VerifyEditDistanceKPrefixHash:
    case VerifyEditDistanceKPrefixHashPointer: {
      return new PQSearcherStage(state);
    }
    case Training: {
      return new PQSearcherStage(state);
    }
    default:
      break;
  }

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

ExecutionTraceManager* ExecutionTraceManagerFactory::create(ClientVerifier* cv) {
  switch (RunMode) {
    case VerifyNaive: {
      return new ExecutionTraceManager(cv);
      break;
    }
    case VerifyEditDistanceRow:
    case VerifyEditDistanceKPrefixRow:
    case VerifyEditDistanceKPrefixHash:
    case VerifyEditDistanceKPrefixTest:
    case VerifyEditDistanceKPrefixHashPointer: {
      return new VerifyExecutionTraceManager(cv);
    }

    case Training: {
      return new TrainingExecutionTraceManager(cv);
    }
  }
  cv_message("cliver mode not supported in ExecutionTraceManager");
  return NULL;
}

////////////////////////////////////////////////////////////////////////////////

ExecutionTraceEditDistanceTree* EditDistanceTreeFactory::create() {
  switch (RunMode) {

    case VerifyEditDistanceRow: {
      return new LevenshteinRadixTree<ExecutionTrace, BasicBlockID>();
      break;
    }

    case VerifyEditDistanceKPrefixRow: {
      return new KLevenshteinRadixTree<ExecutionTrace, BasicBlockID>();
      break;
    }

    case VerifyEditDistanceKPrefixHash: {
      return new KExtensionTree<ExecutionTrace, BasicBlockID>();
      break;
    }

    case VerifyEditDistanceKPrefixHashPointer: {
      return new KExtensionOptTree<ExecutionTrace, BasicBlockID>();
      break;
    }

    case VerifyEditDistanceKPrefixTest: {
      return new EditDistanceTreeEquivalenceTest<ExecutionTrace, BasicBlockID>();
      break;
    }

    default: {
      cv_error("EditDistanceFactory called in non-editdistance mode");
    }

  }
  cv_error("invalid edit distance algorithm");
  return NULL;
}

////////////////////////////////////////////////////////////////////////////////

ExecutionStateProperty* ExecutionStatePropertyFactory::create() {
  switch (RunMode) {
    case VerifyNaive: {
      return new ExecutionStateProperty();
    }
    case VerifyEditDistanceRow:
    case VerifyEditDistanceKPrefixRow:
    case VerifyEditDistanceKPrefixHash:
    case VerifyEditDistanceKPrefixTest:
    case VerifyEditDistanceKPrefixHashPointer: {
      return new EditDistanceExecutionStateProperty();
    }

    case Training: {
      return new ExecutionStateProperty();
    }
  }
}

////////////////////////////////////////////////////////////////////////////////

SocketEventSimilarity* SocketEventSimilarityFactory::create() {
  switch (ClientModel) {
    case Tetrinet: {
      return new SocketEventSimilarityTetrinet();
    }
    case XPilot: {
      return new SocketEventSimilarityDataOnly();
    }
    default: {
      return new SocketEventSimilarity();
    }
  }
}

////////////////////////////////////////////////////////////////////////////////

} // end namespace cliver

