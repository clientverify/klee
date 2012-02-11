//===-- Configuration.cpp----------------------------------------*- C++ -*-===//
//
// <insert license>
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//

#include "CVCommon.h"

#include "cliver/ClientVerifier.h"
#include "cliver/CVSearcher.h"
#include "cliver/ExecutionStateProperty.h"
#include "cliver/ExecutionTree.h"
#include "cliver/NetworkManager.h"
#include "cliver/PathManager.h"
#include "cliver/PathTree.h"

namespace cliver {

////////////////////////////////////////////////////////////////////////////////

llvm::cl::opt<CliverMode> g_cliver_mode("cliver-mode", 
  llvm::cl::desc("Choose the mode in which cliver should run."),
  llvm::cl::values(
    clEnumValN(DefaultMode, "default", 
      "Default mode"),
    clEnumValN(TetrinetMode, "tetrinet", 
      "Tetrinet mode"),
    clEnumValN(DefaultTrainingMode, "training", 
      "Default training mode"),
    clEnumValN(TestTrainingMode, "testtraining", 
      "Test training mode"),
    clEnumValN(OutOfOrderTrainingMode, "out-of-order-training", 
      "Default training mode"),
    clEnumValN(TetrinetTrainingMode, "tetrinet-training", 
      "Tetrinet training mode"),
    clEnumValN(VerifyWithTrainingPaths, "verify-with-paths", 
      "Verify with training paths"),
    clEnumValN(VerifyWithEditCost, "verify-with-edit-cost", 
      "Verify using edit costs"),
  clEnumValEnd),
  llvm::cl::init(DefaultMode));

llvm::cl::opt<SearcherStageMode> g_searcher_stage_mode("searcher-stage-mode",
  llvm::cl::desc("Choose the mode in which cliver search each stage."),
  llvm::cl::values(
    clEnumValN(RandomSearcherStageMode, "random",
      "Random mode"),
    clEnumValN(PQSearcherStageMode, "pq",
      "Priority queue mode"),
    clEnumValN(BFSSearcherStageMode, "bfs",
      "Breadth first mode"),
    clEnumValN(DFSSearcherStageMode, "dfs",
      "Depth first mode"),
  clEnumValEnd),
  llvm::cl::init(BFSSearcherStageMode));

////////////////////////////////////////////////////////////////////////////////

CVSearcher* CVSearcherFactory::create(klee::Searcher* base_searcher, 
                                      ClientVerifier* cv, StateMerger* merger) {
	switch(g_cliver_mode) {
		case DefaultMode:
		case TetrinetMode:
		case XpilotMode:

			return new LogIndexSearcher(NULL, cv, merger);

		case DefaultTrainingMode:

			return new NewTrainingSearcher(cv, merger);
			//return new TrainingSearcher(NULL, cv, merger);

		//case VerifyWithTrainingPaths: {

		//	// Read training paths
		//	PathManagerSet* training_paths = new PathManagerSet();
		//	if (!TrainingPathDir.empty()) {
		//		foreach(std::string path, TrainingPathDir) {
		//			cvstream_->getFiles(path, ".tpath", TrainingPathFile);
		//		}
		//	}
		//	if (TrainingPathFile.empty() || read_training_paths(TrainingPathFile,
		//				training_paths) == 0) {
		//		cv_error("Error reading training path files, exiting now.");
		//	} 

		//	// Construct searcher
		//	pruner_ = new ConstraintPruner();
		//	merger_ = new StateMerger(pruner_);
		//	searcher_ = new VerifySearcher(NULL, merger_, training_paths);

		//}

		case TestTrainingMode:
		case VerifyWithTrainingPaths:
    case VerifyWithEditCost: {

			return new VerifySearcher(cv, merger);

			break;
		}

		case TetrinetTrainingMode:
			cv_error("Tetrinet Training mode is unsupported");
			break;

    default:
			cv_error("Invalid cliver mode!");
      break;
	}
}

SearcherStage* SearcherStageFactory::create(StateMerger* merger, 
                                            CVExecutionState* state) {
	switch (g_searcher_stage_mode) {
    case RandomSearcherStageMode: {
      return new RandomSearcherStage(state);
    }
    case PQSearcherStageMode: {
      return new PQSearcherStage(state);
    }
    case DFSSearcherStageMode: {
      return new DFSSearcherStage(state);
    }
    case BFSSearcherStageMode:
    default: {
      return new BFSSearcherStage(state);
    }
	}
	cv_error("searcher stage mode not supported!");
	return NULL;
}

////////////////////////////////////////////////////////////////////////////////

NetworkManager* NetworkManagerFactory::create(CVExecutionState* state,
                                              ClientVerifier* cv) {
	switch (g_cliver_mode) {
		case DefaultMode:
		case DefaultTrainingMode: 
		case TestTrainingMode: 
    case VerifyWithEditCost: 
		case VerifyWithTrainingPaths: {
			NetworkManager *nm = new NetworkManager(state);
			foreach( SocketEventList *sel, cv->socket_events()) {
				nm->add_socket(*sel);
			}
			return nm;
			break;
		}
		case TetrinetMode: {
			NetworkManagerTetrinet *nm = new NetworkManagerTetrinet(state);
			foreach( SocketEventList *sel, cv->socket_events()) {
				nm->add_socket(*sel);
			}
			return nm;
	  }
		case OutOfOrderTrainingMode: {
			NetworkManagerTraining *nm 
				= new NetworkManagerTraining(state);
			return nm;
		}
		case TetrinetTrainingMode: 
    default:
			break;
	}
	cv_error("cliver mode not supported in NetworkManager");
	return NULL;
}

////////////////////////////////////////////////////////////////////////////////

ExecutionTreeManager* ExecutionTreeManagerFactory::create(ClientVerifier* cv) {
  switch (g_cliver_mode) {
		case DefaultTrainingMode:
			return new TrainingExecutionTreeManager(cv);
		case TestTrainingMode:
			return new TestExecutionTreeManager(cv);
    case VerifyWithEditCost: {
      if (g_searcher_stage_mode != PQSearcherStageMode)
        g_searcher_stage_mode = PQSearcherStageMode;
      return new VerifyExecutionTreeManager(cv);
      break;
    }
		case VerifyWithTrainingPaths:
		case DefaultMode:
    default: {
      return new ExecutionTreeManager(cv);
      break;
    }
  }

	cv_error("cliver mode not supported in ExecutionTreeManager");
	return NULL;
}

////////////////////////////////////////////////////////////////////////////////

PathTree* PathTreeFactory::create(CVExecutionState* root_state) {
  switch (g_cliver_mode) {
		case DefaultTrainingMode:
		case VerifyWithTrainingPaths:
		case DefaultMode:
		default:
			break;
  }
  return new PathTree(root_state);
}

////////////////////////////////////////////////////////////////////////////////

ExecutionStateProperty* ExecutionStatePropertyFactory::create() {
	switch (g_cliver_mode) {
		case DefaultMode:
		case TetrinetMode: 
			return new LogIndexProperty();
			break;
		case DefaultTrainingMode: 
			return new PathProperty();
		case VerifyWithTrainingPaths: 
			return new VerifyProperty();
		case TestTrainingMode: 
    case VerifyWithEditCost:
			return new EditDistanceProperty();
    default:
      break;
	}
	cv_error("invalid cliver mode");
	return NULL;
}

////////////////////////////////////////////////////////////////////////////////

PathManager* PathManagerFactory::create() {
  switch (g_cliver_mode) {
		case DefaultTrainingMode:
			return new TrainingPathManager();
		case VerifyWithTrainingPaths:
    case VerifyWithEditCost:
		case DefaultMode:
    default:
      break;
  }
  return new PathManager();
}

//////////////////////////////////////////////////////////////////////////////

} // end namespace cliver

