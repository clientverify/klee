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

#include "cliver/RadixTree.h"
#include "cliver/EditDistanceTree.h"
#include <limits.h>

#include <vector>
#include <algorithm>
#include <memory>
#include <exception>
#include <iostream>
#include <cmath>

////////////////////////////////////////////////////////////////////////////////

namespace cliver {

////////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////
/// HMMPathPredictor: 
///////////////////////////////////////////////////////////////////////////////
template <class Sequence, class T>
class HMMPathPredictor
{
 public:

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

///////////////////////////////////////////////////////////////////////////////
// Viterbi Decoder where states and emissions are labeled by integers 0...(N-1)
// Caveat: This decoder is 0-indexed whereas MATLAB is 1-indexed.
///////////////////////////////////////////////////////////////////////////////
class ViterbiDecoder {
public:
  ViterbiDecoder(const std::vector<double>& priors,
		 const std::vector<std::vector<double> >& trans,
		 const std::vector<std::vector<double> >& emis);

  // Add new observed emission to the sequence
  void addEmission(int e);
  std::vector<int> getEmissionSequence() const {return emission_sequence;}
  int getSequenceLength() const {return (int)emission_sequence.size();}
  
  // Number of possible states
  int getNumStates() const {return logp_trans.size();}
  // Number of possible emission types (not the length of the emission sequence)
  int getNumEmissions() const {return logp_emis[0].size();}
  std::vector<int> getDecoding() const; // output most likely state sequence

  // Probability of each state for a given zero-indxed round (default:
  // most recent round)
  std::vector<double> getStateProbabilities(int round=-1) const;

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


////////////////////////////////////////////////////////////////////////////////

} // end namespace cliver

////////////////////////////////////////////////////////////////////////////////

#endif // CLIVER_HMM_PATH_PREDICTOR_H

