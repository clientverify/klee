//===-- HMMPathPredictor.h ----------------------------------*- C++ -*-===//
//
// <insert license>
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//
#ifndef CLIVER_HMM_PATH_PREDICTOR_H
#define CLIVER_HMM_PATH_PREDICTOR_H

#include <vector>
#include <string>

#include "cliver/Training.h"
#include "cliver/Socket.h"

////////////////////////////////////////////////////////////////////////////////

namespace cliver {

////////////////////////////////////////////////////////////////////////////////


///////////////////////////////////////////////////////////////////////////////
// Viterbi Decoder where states and emissions are labeled by integers 0...(N-1)
// Caveat: This decoder is 0-indexed whereas MATLAB is 1-indexed.
///////////////////////////////////////////////////////////////////////////////
class ViterbiDecoder {
public:
  ViterbiDecoder() {}
  ViterbiDecoder(const std::vector<double>& priors,
		 const std::vector<std::vector<double> >& trans,
		 const std::vector<std::vector<double> >& emis);

  /*
  // Constructor that trains its own transition and emission matrices.
  ViterbiDecoder(int Nstate, int Nemis,
                 const std::vector<int>& state_seq,
                 const std::vector<int>& emis_seq,
                 double pseudotr = 0.0,
                 double pseudoem = 0.0);
  */

  // Add new observed emission to the sequence
  void addEmission(int e);
  std::vector<int> getEmissionSequence() const {return emission_sequence;}
  int getSequenceLength() const {return (int)emission_sequence.size();}
  
  // Number of possible states
  int getNumStates() const {return logp_trans.size();}
  // Number of possible emission types (not the length of the emission sequence)
  int getNumEmissions() const {return logp_emis[0].size();}
  std::vector<int> getDecoding() const; // output most likely state sequence

  // Probability of each state for a given zero-indexed round (default:
  // most recent round)
  std::vector<double> getStateProbabilities(int round=-1) const;

  // I/O for the HMM priors, transition, and emission matrices
  friend std::ostream& operator<<(std::ostream& os, const ViterbiDecoder& vd);
  friend std::istream& operator>>(std::istream& is, ViterbiDecoder& vd);

  // Self-test (returns 0 on success)
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
/// HMMPathPredictor: 
///////////////////////////////////////////////////////////////////////////////
class HMMPathPredictor
{
public:

  HMMPathPredictor(const std::string& hmm_training_file) { }

  HMMPathPredictor(const std::vector<std::string>& guide_path_files,
                   const std::vector<std::string>& message_files,
                   const std::string& hmm_data_file) { }

  int rounds();

  //===-------------------------------------------------------------------===//
  // Extra methods, testing, utility
  //===-------------------------------------------------------------------===//

  static int test()
  {
    return 0;
  }

private:

  //===-------------------------------------------------------------------===//
  // Internal Methods
  //===-------------------------------------------------------------------===//

  //===-------------------------------------------------------------------===//
  // Member variables
  //===-------------------------------------------------------------------===//

};

////////////////////////////////////////////////////////////////////////////////

} // end namespace cliver

////////////////////////////////////////////////////////////////////////////////

#endif // CLIVER_HMM_PATH_PREDICTOR_H

