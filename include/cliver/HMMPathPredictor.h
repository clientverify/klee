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
#include "cliver/JaccardTree.h"

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

  HMMPathPredictor() {}
  HMMPathPredictor(const std::vector<std::string>& guide_path_files,
                   const std::vector<std::string>& message_files,
                   const std::string& hmm_data_file);
  friend std::istream& operator>>(std::istream& is, HMMPathPredictor& hpp);
  friend std::ostream& operator<<(std::ostream& os,const HMMPathPredictor& hpp);

  int rounds() const {return vd.getSequenceLength();} // num rounds added

  // Add a message and update the probabilities
  void addMessage(const SocketEvent& se);

  // Retrieve the current (or past) guide paths, with a list of likelihoods.
  // Inputs: round (1-indexed), initial BasicBlock id and confidence (0.99 suggested)
  // Outputs: vector of pairs (probability, index of guide path)
  std::vector<std::pair<double,int> >
  predictPath(int round, BasicBlockID bb, double confidence) const;

  // Get the sequence of assigned message cluster IDs
  const std::vector<int>& getAssignedMsgClusters() const
  { return assigned_msg_cluster_ids; }

  // Vector of training objects, for which predictPath returns indices.
  std::vector<std::shared_ptr<TrainingObject> > getAllTrainingObjects()
  { return fragment_medoids; }

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

  double jaccard_distance(const std::set<uint8_t>& s1,
                          const std::set<uint8_t>& s2) const;
  int nearest_message_id(const SocketEvent& se) const;
  std::set<uint8_t> message_as_set(const SocketEvent& se) const;
  int message_direction(const SocketEvent& se) const;

  //===-------------------------------------------------------------------===//
  // Member variables
  //===-------------------------------------------------------------------===//

  ViterbiDecoder vd; // HMM training
  std::vector<std::shared_ptr<TrainingObject> > fragment_medoids; // training
  std::vector<std::shared_ptr<TrainingObject> > message_medoids; // training
  std::vector<std::set<uint8_t> > messages_as_sets; // training
  std::vector<SocketEvent*> messages; // training

  std::vector<int> assigned_msg_cluster_ids;
};

////////////////////////////////////////////////////////////////////////////////

} // end namespace cliver

////////////////////////////////////////////////////////////////////////////////

#endif // CLIVER_HMM_PATH_PREDICTOR_H

