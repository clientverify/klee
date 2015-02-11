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

#include <ghmm/sequence.h>

#include "../../lib/Core/Common.h"
#include "../../lib/Cliver/CVCommon.h"
#include "cliver/ClientVerifier.h"
#include "cliver/CVExecutor.h"
#include "cliver/CVStream.h"
#include "cliver/Training.h"

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

llvm::cl::list<std::string> InputFileListing("input-tpaths",
    llvm::cl::Optional,
    llvm::cl::ValueRequired,
    llvm::cl::desc("Specify a file that lists training path files (.tpath)"), 
    llvm::cl::value_desc("tpath file listing"));

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

// Viterbi Decoder where states and emissions are labeled by integers 0...(N-1)
// Caveat: This decoder is 0-indexed whereas MATLAB is 1-indexed.
class ViterbiDecoder {
public:
  ViterbiDecoder(const std::vector<double>& priors,
		 const std::vector<std::vector<double> >& trans,
		 const std::vector<std::vector<double> >& emis) :
    logp_priors(priors), logp_trans(trans), logp_emis(emis),
    viterbi_table(trans.size(), std::vector<double>()),
    backward_links(trans.size(), std::vector<int>())
  {
    for (size_t i = 0; i < logp_priors.size(); ++i)
      logp_priors[i] = log(logp_priors[i]);

    for (size_t i = 0; i < logp_trans.size(); ++i)
      for (size_t j = 0; j < logp_trans[i].size(); ++j)
	logp_trans[i][j] = log(logp_trans[i][j]);
    
    for (size_t i = 0; i < logp_emis.size(); ++i)
      for (size_t j = 0; j < logp_emis[i].size(); ++j)
	logp_emis[i][j] = log(logp_emis[i][j]);
  }
  ~ViterbiDecoder();

  // Add new observed emission
  void addEmission(int e);
  void addEmissionSequence(std::vector<int> ee);
  
  // could change with future info
  std::vector<int> getDecoding() const; // output most likely state sequence
  int getDecodingFinalState() const; // get final state of decoding only

private:
  std::vector<double> logp_priors; // initial log probabilities
  std::vector<std::vector<double> > logp_trans; // transition log probabilities
  std::vector<std::vector<double> > logp_emis; // emission log probabilities

  std::vector<std::vector<double> > viterbi_table;
  std::vector<std::vector<int> > backward_links;
};

int max_element_in_column(const std::vector<std::vector<double> > m, int col)
{
  double current_max = - std::numeric_limits<double>::max();
  int current_max_index = 0;
  for (int i = 0; i < (int)m.size(); ++i) {
    if (m[i][col] > current_max) {
      current_max = m[i][col];
      current_max_index = i;
    }
  }
  return current_max_index;
}

int DoHMMPredict()
{
  using namespace cliver;

  std::set<TrainingObject*> training_objects;
  std::vector<std::string> input_files;

  // Read tpath files (cluster medoids)
  if (InputFileListing.size() != 1) {
    cv_error("HMMPredict requires an input file listing.\n");
    return 2;
  }
  std::string input_file_listing = InputFileListing[0];
  CVMESSAGE("Opening tpath file listing: " << input_file_listing);
  std::ifstream infile(input_file_listing);
  std::string single_path;
  while (infile >> single_path) {
    input_files.push_back(single_path);
  }
  CVMESSAGE("Found " << input_files.size() << " paths in "
	    << input_file_listing);

  CVMESSAGE("Reading tpath files");
  TrainingManager::read_files(input_files, training_objects); 
  CVMESSAGE("Successfully read " << training_objects.size() << " files.");

  // foreach (TrainingObject *tobj, training_objects)
  // {
  //   CVMESSAGE(*tobj);
  //   CVMESSAGE("(" << tobj->name << ") " << tobj->trace);
  // }
}

void sequence_alloc_print(void)
{
  ghmm_dseq* seq_array;
  int i;

  seq_array= ghmm_dseq_calloc(1);
  seq_array->seq_len[0]=10;
  seq_array->seq_id[0]=101.0;
  seq_array->seq[0]=(int*)malloc(seq_array->seq_len[0]*sizeof(int));

  for (i=0; i<seq_array->seq_len[0]; i++)
    seq_array->seq[0][i]=1;

  ghmm_dseq_print_xml(seq_array, stdout);

  ghmm_dseq_free(&seq_array);
}


//===----------------------------------------------------------------------===//
// main
//===----------------------------------------------------------------------===//
int main(int argc, char **argv, char **envp) {
  using namespace llvm;

  llvm::cl::ParseCommandLineOptions(argc, argv, " hmmtrain\n");

  bool no_output = true;
  std::string output_dir("./hmmtrain-out");
  g_cvstream = new cliver::CVStream(no_output, output_dir);
  g_cvstream->init();
  int ret = 0;

  switch (StatsMode)
  {
    case HMMTest:
      {
        sequence_alloc_print();
        break;
      }
    case HMMTrain:
      {
        sequence_alloc_print();
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

