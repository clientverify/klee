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
#include <cmath>

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

public:
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

  enum MessageMetric {
    JACCARD,
    MJACCARD,
    RUZICKA
  };

  HMMPathPredictor(MessageMetric m=RUZICKA, int hlen=40) :
    header_length(hlen), metric(m)
  {
    if (m == MJACCARD) {
      header_length = 0; // don't use exponentially decaying weights
    }
  }
  friend std::istream& operator>>(std::istream& is, HMMPathPredictor& hpp);
  friend std::ostream& operator<<(std::ostream& os,const HMMPathPredictor& hpp);

  int rounds() const {return vd.getSequenceLength();} // num rounds added

  // Add a message and update the probabilities
  void addMessage(const SocketEvent& se, BasicBlockID bb);

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
    HMMPathPredictor hpp;
    double TOL = 1e-6;
    double d;

    std::set<uint8_t> set1, set2;
    set1.insert(1);
    set1.insert(2);
    set2.insert(2);
    set2.insert(3);
    set2.insert(4);
    d = hpp.jaccard_distance(set1, set2);
    std::cout << "Jaccard({1,2},{2,3,4}) = " << d << "\n";
    if (fabs(d - 3.0/4.0) > TOL)
      return 1;

    std::map<uint8_t,double> s1, s2;
    s1[1] = 2.0;
    s1[2] = 3.0;
    s2[2] = 6.0;
    s2[3] = 1.0;
    d = hpp.ruzicka_distance(s1, s2);
    std::cout << "Ruzicka({1:2.0, 2:3.0}, {2:6.0, 3:1.0}) = " << d << "\n";
    if (fabs(d - 6.0/9.0) > TOL)
      return 2;

    const char *x = "hello";
    const char *y = "xello";
    SocketEvent xx((const unsigned char*)x, strlen(x));
    SocketEvent yy((const unsigned char*)y, strlen(y));
    std::map<uint8_t,double> x_hist = hpp.message_as_hist(xx);
    std::map<uint8_t,double> y_hist = hpp.message_as_hist(yy);
    d = hpp.ruzicka_distance(x_hist, y_hist);
    std::cout << "Ruzicka('hello', 'xello') = " << d << "\n";
    double r = std::pow(0.5, 1.0/40);
    double expected_union = 2.0 + r*(1.0 - std::pow(r,4))/(1.0-r);
    if (fabs(d - 2.0/expected_union) > TOL)
      return 3;

    y = "hella";
    yy = SocketEvent((const unsigned char*)y, strlen(y));
    y_hist = hpp.message_as_hist(yy);
    d = hpp.ruzicka_distance(x_hist, y_hist);
    std::cout << "Ruzicka('hello', 'hella') = " << d << "\n";
    expected_union = (1.0 - std::pow(r,5))/(1.0 - r) + std::pow(r,4);
    if (fabs(d - 2.0*std::pow(r,4)/expected_union) > TOL)
      return 4;

    return 0;
  }

private:

  //===-------------------------------------------------------------------===//
  // Internal Methods
  //===-------------------------------------------------------------------===//

  double jaccard_distance(const std::set<uint8_t>& s1,
                          const std::set<uint8_t>& s2) const;
  double ruzicka_distance(const std::map<uint8_t,double>& s1,
                          const std::map<uint8_t,double>& s2) const;
  int nearest_message_id(const SocketEvent& se, BasicBlockID bb) const;
  std::set<uint8_t> message_as_set(const SocketEvent& se) const;
  std::map<uint8_t,double> message_as_hist(const SocketEvent& se) const;
  int message_direction(const SocketEvent& se) const;

  //===-------------------------------------------------------------------===//
  // Member variables
  //===-------------------------------------------------------------------===//

  int header_length = 16; // estimated header length for weighted histogram
  MessageMetric metric = RUZICKA; // metric to use for message clustering

  ViterbiDecoder vd; // HMM training
  std::vector<std::shared_ptr<TrainingObject> > fragment_medoids; // training
  std::vector<std::shared_ptr<TrainingObject> > message_medoids; // training
  std::vector<std::set<uint8_t> > messages_as_sets; // training
  std::vector<std::map<uint8_t,double> > messages_as_hist; // training
  std::vector<SocketEvent*> messages; // training

  // observations
  std::vector<int> assigned_msg_cluster_ids;
  std::vector<SocketEvent::Type> directions;

  // debug
  std::vector<std::string> fragment_medoid_files;
  std::vector<std::string> message_medoid_files;
};

////////////////////////////////////////////////////////////////////////////////

} // end namespace cliver

////////////////////////////////////////////////////////////////////////////////

#endif // CLIVER_HMM_PATH_PREDICTOR_H

