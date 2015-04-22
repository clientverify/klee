//===-- tools/cliverstats/main.cpp ------------------------------*- C++ -*-===//
//
// <insert license>
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//

#include <stdlib.h>

#if (LLVM_VERSION_MAJOR == 2 && LLVM_VERSION_MINOR < 7)
#error
#endif

#include "../../lib/Core/Common.h"
// #include "../../lib/Cliver/CVCommon.h"
#include "cliver/ClientVerifier.h"
#include "cliver/CVExecutor.h"
#include "cliver/CVStream.h"
#include "cliver/Training.h"
#include "cliver/JaccardTree.h"
#include "cliver/HMMPathPredictor.h"

#include "klee/Config/config.h"
#include "klee/ExecutionState.h"
#include "klee/Expr.h"
#include "klee/Internal/ADT/KTest.h"
#include "klee/Internal/ADT/TreeStream.h"
#include "klee/Internal/Support/ModuleUtil.h"
#include "klee/Internal/System/Time.h"
#include "klee/Interpreter.h"
#include "klee/Statistics.h"

#if LLVM_VERSION_CODE > LLVM_VERSION(3, 2)
#include "llvm/IR/Constants.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LLVMContext.h"
#else
#include "llvm/Constants.h"
#include "llvm/Module.h"
#include "llvm/Type.h"
#include "llvm/InstrTypes.h"
#include "llvm/Instruction.h"
#include "llvm/Instructions.h"
#include "llvm/LLVMContext.h"
#include "llvm/Support/FileSystem.h"
#endif
#include "llvm/Support/FileSystem.h"
#include "llvm/Bitcode/ReaderWriter.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/raw_ostream.h"

#if LLVM_VERSION_CODE < LLVM_VERSION(3, 0)
#include "llvm/Target/TargetSelect.h"
#else
#include "llvm/Support/TargetSelect.h"
#endif
#include "llvm/Support/Signals.h"

#if LLVM_VERSION_CODE < LLVM_VERSION(3, 5)
#include "llvm/Support/system_error.h"
#endif

#include <dirent.h>
#include <signal.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include <cerrno>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <sstream>
#include <limits>
#include <cmath>
#include <algorithm>

namespace {

enum StatsModeType { HMMTest, HMMTrain, HMMPredict };
llvm::cl::opt<StatsModeType>
StatsMode("mode", 
    llvm::cl::desc("Select mode:"),
    llvm::cl::values(
      clEnumValN(HMMTest, "hmmtest", "HMM self test"),
      clEnumValN(HMMTrain, "hmmtrain", "HMM train"),
      clEnumValN(HMMPredict, "hmmpredict", "HMM predict"),
      clEnumValEnd),
    llvm::cl::init(HMMTest));

llvm::cl::list<std::string> HMMTrainingFiles("hmm-training",
    llvm::cl::Optional,
    llvm::cl::ValueRequired,
    llvm::cl::desc("Specify an HMM training file"), 
    llvm::cl::value_desc("HMM training file"));

llvm::cl::list<std::string> HMMTestMessages("hmm-test-msg",
    llvm::cl::ZeroOrMore,
    llvm::cl::ValueRequired,
    llvm::cl::desc("Specify tpath file that consists of test message"), 
    llvm::cl::value_desc("tpath file with message to test"));

llvm::cl::list<std::string> InputLabels("input-clusters",
    llvm::cl::ZeroOrMore,
    llvm::cl::ValueRequired,
    llvm::cl::desc("Specify a file containing input cluster labels "
		   "(1 through N) corresponding to tpath fragments"),
    llvm::cl::value_desc("execution fragment cluster labels"));

llvm::cl::list<std::string> InputPriors("input-prior",
    llvm::cl::ZeroOrMore,
    llvm::cl::ValueRequired,
    llvm::cl::desc("Specify a file containing the starting probability "
		   "of each state (1 through N)"),
    llvm::cl::value_desc("starting probabilities file"));

llvm::cl::list<std::string> InputTransitionMatrix("input-trans",
    llvm::cl::ZeroOrMore,
    llvm::cl::ValueRequired,
    llvm::cl::desc("Specify a file containing the transition matrix "
		   "(NxN) between"),
    llvm::cl::value_desc("HMM state transition matrix"));

llvm::cl::list<std::string> InputEmissionMatrix("input-emis",
    llvm::cl::ZeroOrMore,
    llvm::cl::ValueRequired,
    llvm::cl::desc("Specify a file containing emission probabilities (NxE)"),
    llvm::cl::value_desc("HMM emission probabilities"));

}

cliver::CVStream *g_cvstream;


int DoHMMTest()
{
  using namespace std;
  using namespace cliver;
  int ret = 0;
  int r;
  cout << "Running HMM self-test...\n";
  r = ViterbiDecoder::test();
  ret += r;
  cout << "HMM self-test " << (r==0 ? "succeeded" : "failed")
       << "!\n\n";

  cout << "Running JaccardTree self-test...\n";
  r = JaccardTree<vector<int>,int>::test();
  ret += r;
  cout << "JaccardTree self-test " << (r==0 ? "succeeded" : "failed")
       << "!\n\n";

  cout << "Running MultiSetJaccardTree self-test...\n";
  r = MultiSetJaccardTree<vector<int>,int>::test();
  ret += r;
  cout << "MultiSetJaccardTree self-test " << (r==0 ? "succeeded" : "failed")
       << "!\n\n";

  cout << "Running MultiSetJaccardPrefixTree self-test...\n";
  r = MultiSetJaccardPrefixTree<vector<int>,int>::test();
  ret += r;
  cout << "MultiSetJaccardPrefixTree self-test "
       << (r==0 ? "succeeded" : "failed") << "!\n\n";

  cout << "Running ApproxEditDistanceTree self-test...\n";
  r = ApproxEditDistanceTree<vector<int>,int>::test();
  ret += r;
  cout << "ApproxEditDistanceTree self-test "
       << (r==0 ? "succeeded" : "failed") << "!\n\n";

  return ret;
}

int DoHMMPredict()
{
  using namespace cliver;
  using namespace std;

  std::set<TrainingObject*> training_objects;
  std::vector<std::string> input_files;

  // // Read tpath files (cluster medoids)
  // if (InputFileListing.size() != 1) {
  //   cv_error("HMMPredict requires an input file listing.\n");
  //   return 2;
  // }
  // std::string input_file_listing = InputFileListing[0];
  // CVMESSAGE("Opening tpath file listing: " << input_file_listing);
  // std::ifstream infile(input_file_listing);
  // std::string single_path;
  // while (infile >> single_path) {
  //   input_files.push_back(single_path);
  // }
  // CVMESSAGE("Found " << input_files.size() << " paths in "
  //           << input_file_listing);

  // CVMESSAGE("Reading tpath files");
  // TrainingManager::read_files(input_files, training_objects); 
  // CVMESSAGE("Successfully read " << training_objects.size() << " files.");

  

  // foreach (TrainingObject *tobj, training_objects)
  // {
  //   CVMESSAGE(*tobj);
  //   CVMESSAGE("(" << tobj->name << ") " << tobj->trace);
  // }

  if (HMMTrainingFiles.size() != 1) {
    cv_error("HMMPredict requires an HMM training file.\n");
    return 2;
  }
  std::string hmm_training_file = HMMTrainingFiles[0];
  CVMESSAGE("Opening HMM training file: " << hmm_training_file);
  std::ifstream infile(hmm_training_file);
  HMMPathPredictor hpp;
  infile >> hpp;
  std::cout << hpp;
  
  if (HMMTestMessages.size() > 0) {
    CVMESSAGE("Testing " << HMMTestMessages.size() << " message files");
    TrainingObjectSet dummy;
    std::vector<TrainingObject*> test_objects;
    TrainingManager::read_files(HMMTestMessages, dummy); //HACK!
    TrainingManager::read_files_in_order(HMMTestMessages, test_objects);
    auto all_training_objects = hpp.getAllTrainingObjects();

    for (size_t i = 0; i < HMMTestMessages.size(); ++i) {
      cout << "------------------------------------------------\n";
      cout << "Adding message: " << HMMTestMessages[i] << "\n";
      TrainingObject* tobj = test_objects[i];
      SocketEvent* se = *(tobj->socket_event_set.begin());
      hpp.addMessage(*se);
      const vector<int>& msg_cluster_ids = hpp.getAssignedMsgClusters();
      cout << "Message assigned to cluster: " << msg_cluster_ids[i] << "\n";
      auto guide_paths = hpp.predictPath((int)i+1, 0, 0.99);
      for (auto it = guide_paths.begin(); it != guide_paths.end(); ++it) {
        pair<double, int> entry(*it);
        std::cout.precision(6);
        std::cout.setf( std::ios::fixed, std:: ios::floatfield );
        cout << "Prob = " << entry.first << " | guide_path " << entry.second
             << " | " << all_training_objects[entry.second]->name << "\n";
      }
    }

  }

  return 0;
}

//===----------------------------------------------------------------------===//
// main
//===----------------------------------------------------------------------===//
int main(int argc, char **argv, char **envp) {
  using namespace llvm;

  llvm::cl::ParseCommandLineOptions(argc, argv, " hmmtool\n");

  bool no_output = true;
  std::string output_dir("./hmmtool-out");
  g_cvstream = new cliver::CVStream(no_output, output_dir);
  g_cvstream->init();
  int ret = 0;

  switch (StatsMode)
  {
    case HMMTest:
      {
	ret = DoHMMTest();
        break;
      }
    case HMMTrain:
      {
        // TODO: do we need to train in C++ or is python sufficient?
        break;
      }
    case HMMPredict:
      {
        ret = DoHMMPredict();
        break;
      }

  }

  return ret;

}

