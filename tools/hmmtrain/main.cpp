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

///////////////////////////////////////////////////////////////////////////////
// Viterbi Decoder where states and emissions are labeled by integers 0...(N-1)
// Caveat: This decoder is 0-indexed whereas MATLAB is 1-indexed.
///////////////////////////////////////////////////////////////////////////////
class ViterbiDecoder {
public:
  ViterbiDecoder(const std::vector<double>& priors,
		 const std::vector<std::vector<double> >& trans,
		 const std::vector<std::vector<double> >& emis);

  // Add new observed emission
  void addEmission(int e);
  
  // Retrieve results (note that the optimal decoding will change as
  // more emissions are added).
  int getNumStates() const {return logp_trans.size();}
  int getNumEmissions() const {return logp_emis[0].size();}
  std::vector<int> getDecoding() const; // output most likely state sequence
  std::vector<double> getFinalStateProbabilities() const; // P for each state
  std::vector<int> getEmissionHistory() const {return emission_sequence;}

  // Self-test
  static int test();

private:
  std::vector<double> logp_priors; // initial log probabilities
  std::vector<std::vector<double> > logp_trans; // transition log probabilities
  std::vector<std::vector<double> > logp_emis; // emission log probabilities

  std::vector<std::vector<double> > viterbi_table;
  std::vector<std::vector<int> > backward_links; // pointers to previous state
  std::vector<int> emission_sequence; // history of emissions added
};
///////////////////////////////////////////////////////////////////////////////
// Viterbi Decoder implementation
///////////////////////////////////////////////////////////////////////////////
template<class T>
size_t max_element_in_column(const std::vector<std::vector<T> >& m, size_t col)
{
  T current_max = std::numeric_limits<T>::lowest();
  size_t current_max_index = 0;
  for (size_t i = 0; i < m.size(); ++i) {
    if (m[i][col] > current_max) {
      current_max = m[i][col];
      current_max_index = i;
    }
  }
  return current_max_index;
}

template<class T>
int max_element_in_vector(const std::vector<T>& v)
{
  return (int)std::distance(v.begin(), std::max_element(v.begin(), v.end()));
}

template<class T>
std::vector<T> extract_column(const std::vector<std::vector<T> >& m, int col)
{
  std::vector<T> column;
  for (size_t i = 0; i < m.size(); ++i)
    column.push_back(m[i][col]);
  return column;
}

static
double safelog(double x)
{
  if (x <= 0.0)
    return -1000000.0;
  else
    return log(x);
}

// Equivalent to log(sum(exp(vec))) without underflow problems.
static
double logsum(std::vector<double> v)
{
  double max = *std::max_element(v.begin(), v.end());
  double sum = 0.0;
  for (size_t i = 0; i < v.size(); ++i)
    sum += exp(v[i] - max);
  return log(sum) + max;
}

template<class T>
void print_vector(const std::vector<T>& v)
{
  for (size_t i = 0; i < v.size(); ++i)
    std::cout << v[i] << ' ';
  std::cout << '\n';
}

template<class T>
void print_matrix(const std::vector<std::vector<T> >& m)
{
  for (size_t i = 0; i < m.size(); ++i)
    print_vector(m[i]);
}

ViterbiDecoder::ViterbiDecoder(const std::vector<double>& priors,
			       const std::vector<std::vector<double> >& trans,
			       const std::vector<std::vector<double> >& emis) :
  logp_priors(priors), logp_trans(trans), logp_emis(emis),
  viterbi_table(trans.size(), std::vector<double>()),
  backward_links(trans.size(), std::vector<int>())
{
  for (size_t i = 0; i < logp_priors.size(); ++i)
    logp_priors[i] = safelog(logp_priors[i]);

  for (size_t i = 0; i < logp_trans.size(); ++i)
    for (size_t j = 0; j < logp_trans[i].size(); ++j)
      logp_trans[i][j] = safelog(logp_trans[i][j]);
    
  for (size_t i = 0; i < logp_emis.size(); ++i)
    for (size_t j = 0; j < logp_emis[i].size(); ++j)
      logp_emis[i][j] = safelog(logp_emis[i][j]);
}

void
ViterbiDecoder::addEmission(int e)
{
  assert(e < (int)logp_emis[0].size());
  bool first_emission = emission_sequence.empty();
  emission_sequence.push_back(e);

  if (first_emission) {
    // Add first column to the viterbi table (dynamic programming).
    // The probability of the first state being 'i' is the prior
    // probability of starting in state 'i' times the emission
    // probability i->e.
    for (size_t i = 0; i < viterbi_table.size(); ++i) {
      viterbi_table[i].push_back(logp_priors[i] + logp_emis[i][e]);
      backward_links[i].push_back(-1); // start state has no previous state
    }
  }
  else {
    // Grab last column
    int lastcol = (int)viterbi_table[0].size()-1;
    std::vector<double> previous_logp = extract_column(viterbi_table, lastcol);
  
    // Add a column to the viterbi table (dynamic programming),
    // while updating the backward links.
    for (size_t i = 0; i < viterbi_table.size(); ++i) {
      std::vector<double> candidate_logp;
      // For each possible previous state 'j', compute the probability
      // that the next state is 'i' based on the transition probability
      // j->i and the emission probability i->e.
      for (size_t j = 0; j < viterbi_table.size(); ++j) {
	candidate_logp.push_back(previous_logp[j] +
				 logp_trans[j][i] +
				 logp_emis[i][e]);
      }
      int winner = max_element_in_vector(candidate_logp);
      viterbi_table[i].push_back(candidate_logp[winner]);
      backward_links[i].push_back(winner);
    }
  }
  
  return;
}

std::vector<double>
ViterbiDecoder::getFinalStateProbabilities() const
{
  // Normalize and exponentiate log probabilities in the final column
  std::vector<double> final_p;
  // no input data
  if (emission_sequence.empty()) {
    for (size_t i = 0; i < logp_priors.size(); ++i) {
      final_p.push_back(exp(logp_priors[i]));
    }
    return final_p;
  }
  // normal operation
  std::vector<double> final_logp =
    extract_column(viterbi_table, (int)viterbi_table[0].size()-1);
  double sum_logp = logsum(final_logp);
  for (size_t i = 0; i < final_logp.size(); ++i) {
    double log_prob = final_logp[i] - sum_logp;
    final_p.push_back(exp(log_prob));
  }
  return final_p;
}

std::vector<int>
ViterbiDecoder::getDecoding() const
{
  std::vector<int> decoding;
  size_t lastcol = viterbi_table[0].size()-1;
  int winner = (int)max_element_in_column(viterbi_table, lastcol);
  decoding.push_back(winner);
  for (size_t col = lastcol; col > 0; --col) {
    winner = backward_links[winner][col];
    decoding.push_back(winner);
  }
  std::reverse(decoding.begin(), decoding.end());
  assert(decoding.size() == emission_sequence.size());
  return decoding;
}

// Replicate matlab example
// trans = [0.95,0.05;
//          0.10,0.90];
// emis = [1/6 1/6 1/6 1/6 1/6 1/6;
//    1/10 1/10 1/10 1/10 1/10 1/2];
// [seq,states] = hmmgenerate(10,trans,emis);
// estimatedStates = hmmviterbi(seq,trans,emis);
int
ViterbiDecoder::test()
{
  using namespace std;
  using namespace cliver;
  vector<vector<double> > trans, emis;
  trans.push_back(vector<double>({0.95,0.05}));
  trans.push_back(vector<double>({0.10,0.90}));
  const double x = 1.0/6.0; //fair
  emis.push_back(vector<double>({x,x,x,x,x,x}));
  emis.push_back(vector<double>({0.01,0.01,0.01,0.01,0.01,0.95}));
  vector<double> priors({0.4,0.6});
  vector<int> seq({4,1,3,4,5,5,3,0,0,1,5,1,4,1,5,2,5,5,5,5});
  vector<int> correct_states({0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1});

  ViterbiDecoder vd(priors, trans, emis);
  for (size_t i = 0; i < seq.size(); ++i)
    vd.addEmission(seq[i]);
  vector<int> estimated_states = vd.getDecoding();

  cout << "Transition matrix:\n";
  print_matrix(trans);
  cout << "Emission matrix:\n";
  print_matrix(emis);
  cout << "Priors: ";
  print_vector(priors);
  cout << "Emission sequence: ";
  print_vector(seq);
  cout << "Correct states:    ";
  print_vector(correct_states);
  cout << "Estimated states:  ";
  print_vector(estimated_states);
  cout << "Viterbi table:\n";
  print_matrix(vd.viterbi_table);
  cout << "Backward links:\n";
  print_matrix(vd.backward_links);
  cout << "Final state probabilities: ";
  print_vector(vd.getFinalStateProbabilities());

  // Check answers
  for (size_t i = 0; i < correct_states.size(); ++i) {
    if (estimated_states[i] != correct_states[i]) {
      return 2;
    }
  }
  return 0;
}
///////////////////////////////////////////////////////////////////////////////


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

  return 0;
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
	std::cout << "Running HMM self-test...\n";
	ret += ViterbiDecoder::test();
	std::cout << "HMM self-test " << (ret==0 ? "succeeded" : "failed")
		  << "!\n";
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

