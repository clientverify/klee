//===-- HMMPathPredictor.cpp ------------------------------------*- C++ -*-===//
//
// <insert license>
//
//===----------------------------------------------------------------------===//

#include "cliver/HMMPathPredictor.h"

#include <limits>
#include <algorithm>
#include <iostream>
#include <sstream>
#include <cmath>
#include <cassert>

////////////////////////////////////////////////////////////////////////////////

namespace cliver {

////////////////////////////////////////////////////////////////////////////////


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
ViterbiDecoder::getStateProbabilities(int round) const
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
  if (round == -1) { // default: most recent round
    round = (int)viterbi_table[0].size()-1;
  }
  std::vector<double> final_logp = extract_column(viterbi_table, round);
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

std::ostream& operator<<(std::ostream& os, const ViterbiDecoder& vd)
{
  int num_states = vd.getNumStates();
  int num_emis = vd.getNumEmissions();
  os << "NumStates: " << num_states << "\n";
  os << "NumEmissions: " << num_emis << "\n";
  os << "Priors:\n";
  for (size_t i = 0; i < vd.logp_priors.size(); ++i) {
    os << exp(vd.logp_priors[i]) << " ";
  }
  os << "\n";
  os << "TransitionMatrix:\n";
  for (size_t i = 0; i < vd.logp_trans.size(); ++i) {
    for (size_t j = 0; j < vd.logp_trans[i].size(); ++j) {
      os << exp(vd.logp_trans[i][j]) << " ";
    }
    os << "\n";
  }
  os << "EmissionMatrix:\n";
  for (size_t i = 0; i < vd.logp_emis.size(); ++i) {
    for (size_t j = 0; j < vd.logp_emis[i].size(); ++j) {
      os << exp(vd.logp_emis[i][j]) << " ";
    }
    os << "\n";
  }
  return os;
}

std::istream& operator>>(std::istream& is, ViterbiDecoder& vd)
{
  std::string marker;
  int num_states, num_emis;
  double p;
  
  is >> marker;
  assert(marker == "NumStates:");
  is >> num_states;
  
  is >> marker;
  assert(marker == "NumEmissions:");
  is >> num_emis;

  is >> marker;
  assert(marker == "Priors:");
  vd.logp_priors.clear();
  for (int i = 0; i < num_states; i++) {
    is >> p;
    vd.logp_priors.push_back(safelog(p));
  }

  is >> marker;
  assert(marker == "TransitionMatrix:");
  vd.logp_trans.clear();
  for (int i = 0; i < num_states; i++) {
    vd.logp_trans.push_back(std::vector<double>());
    for (int j = 0; j < num_states; j++) {
      is >> p;
      vd.logp_trans[i].push_back(safelog(p));
    }
  }

  is >> marker;
  assert(marker == "EmissionMatrix:");
  vd.logp_emis.clear();
  for (int i = 0; i < num_states; i++) {
    vd.logp_emis.push_back(std::vector<double>());
    for (int j = 0; j < num_emis; j++) {
      is >> p;
      vd.logp_emis[i].push_back(safelog(p));
    }
  }

  vd.emission_sequence.clear();
  vd.viterbi_table =
    std::vector<std::vector<double> >(num_states, std::vector<double>());
  vd.backward_links =
    std::vector<std::vector<int> >(num_states, std::vector<int>());

  return is;
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
  ViterbiDecoder vd2;
  ostringstream oss;
  oss << vd;
  istringstream iss(oss.str());
  iss >> vd2;
  for (size_t i = 0; i < seq.size(); ++i) {
    vd.addEmission(seq[i]);
    vd2.addEmission(seq[i]);
  }
  vector<int> estimated_states = vd.getDecoding();

  cout << vd;
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
  for (int i = 0; i < vd.getSequenceLength(); ++i) {
    cout << "State probabilities for round " << i << ": ";
    print_vector(vd.getStateProbabilities(i));
  }
  cout << "State probabilities for final round: ";
  print_vector(vd.getStateProbabilities());

  cout << "State probabilities for final round(vd2): ";
  print_vector(vd2.getStateProbabilities());


  // Check answers
  for (size_t i = 0; i < correct_states.size(); ++i) {
    if (estimated_states[i] != correct_states[i]) {
      return 2;
    }
  }
  vector<double> pfinal = vd.getStateProbabilities();
  vector<double> pfinal2 = vd2.getStateProbabilities();
  for (size_t i = 0; i < pfinal.size(); ++i) {
    if (abs(pfinal[i] - pfinal2[i]) > 1e-5) {
      return 3;
    }
  }
  
  return 0;
}

///////////////////////////////////////////////////////////////////////////////
/// HMMPathPredictor Implementation
///////////////////////////////////////////////////////////////////////////////



std::istream& operator>>(std::istream& is, HMMPathPredictor& hpp)
{
  using namespace std;
  
  string item;
  vector<string> fragment_medoid_files;
  vector<string> message_medoid_files;
  std::vector<TrainingObject*> training_objects;
  TrainingObjectSet dummy;// THIS IS A HACK!

  // Read in HMM coefficients  
  is >> hpp.vd;

  // Read in fragment medoids (guide paths)
  is >> item;
  assert(item == "FragmentMedoids:");
  for (int i = 0; i < hpp.vd.getNumStates(); ++i) {
    is >> item;
    fragment_medoid_files.push_back(item);
  }
  TrainingManager::read_files(fragment_medoid_files, dummy); //HACK!
  TrainingManager::read_files_in_order(fragment_medoid_files, training_objects);
  for (auto it = training_objects.begin();
       it != training_objects.end(); ++it) {
    hpp.fragment_medoids.push_back(shared_ptr<TrainingObject>(*it));
  }

  // Read in message medoids (for comparison to incoming message)
  is >> item;
  assert(item == "MessageMedoids:");
  for (int i = 0; i < hpp.vd.getNumEmissions(); ++i) {
    is >> item;
    message_medoid_files.push_back(item);
  }
  training_objects.clear();
  TrainingManager::read_files(message_medoid_files, dummy); //HACK!
  TrainingManager::read_files_in_order(message_medoid_files, training_objects);
  for (auto it = training_objects.begin();
       it != training_objects.end(); ++it) {
    shared_ptr<TrainingObject> tobj(*it);
    hpp.message_medoids.push_back(tobj);
    assert(tobj->socket_event_set.size() >= 1);
    if (tobj->socket_event_set.size() > 1) {
      cerr << "Warning: socket event set for "
           << tobj->name << " has size " << tobj->socket_event_set.size()
           << "\n";
    }
    SocketEvent *se = *(tobj->socket_event_set.begin());
    hpp.messages.push_back(se);
    set<uint8_t> message_as_set(se->data.begin(), se->data.end());
    hpp.messages_as_sets.push_back(message_as_set);
  }
  
  return is;
}

std::ostream& operator<<(std::ostream& os,const HMMPathPredictor& hpp)
{
  using namespace std;
  os << hpp.vd;
  os << "FragmentMedoids:\n";
  for (size_t i = 0; i < hpp.fragment_medoids.size(); ++i) {
    os << hpp.fragment_medoids[i]->name << "\n";
  }
  os << "MessageMedoids:\n";
  for (size_t i = 0; i < hpp.message_medoids.size(); ++i) {
    os << hpp.message_medoids[i]->name << "\n";
  }
  return os;
}

std::set<uint8_t>
HMMPathPredictor::message_as_set(const SocketEvent& se) const
{
  return std::set<uint8_t>(se.data.begin(), se.data.end());
}

int
HMMPathPredictor::message_direction(const SocketEvent& se) const
{
  return se.type;
}

double
HMMPathPredictor::jaccard_distance(const std::set<uint8_t>& s1,
                                   const std::set<uint8_t>& s2) const
{
  using namespace std;
  set<uint8_t> intersect;
  set_intersection(s1.begin(),s1.end(),s2.begin(),s2.end(),
                   inserter(intersect,intersect.begin()));
  set<uint8_t> unionset;
  set_union(s1.begin(),s1.end(),s2.begin(),s2.end(),
            inserter(unionset,unionset.begin()));
  return 1.0 - (double)intersect.size()/(double)unionset.size();
}

int
HMMPathPredictor::nearest_message_id(const SocketEvent& se) const
{
  using namespace std;
  double min_distance = 2.0;
  int min_index = -1;

  set<uint8_t> query_set(message_as_set(se));
  for (size_t i = 0; i < messages_as_sets.size(); ++i) {
    double d;
    if (message_direction(se) != message_direction(*(messages[i]))) {
      d = 1.0;
    }
    else {
      d = jaccard_distance(query_set, messages_as_sets[i]);
    }
    if (d < min_distance) {
      min_distance = d;
      min_index = (int)i;
    }
  }
  return min_index;
}

void
HMMPathPredictor::addMessage(const SocketEvent& se)
{
  int cluster_assignment = nearest_message_id(se);
  assigned_msg_cluster_ids.push_back(cluster_assignment);
  vd.addEmission(cluster_assignment);
}

template <typename T>
std::vector<size_t> sort_indices_reverse(const std::vector<T> &v) {
  using namespace std;
  
  // initialize original index locations
  vector<size_t> idx(v.size());
  for (size_t i = 0; i != idx.size(); ++i) idx[i] = i;

  // sort indices based on comparing values in v
  sort(idx.begin(), idx.end(),
       [&v](size_t i1, size_t i2) {return v[i1] > v[i2];});

  return idx;
}

std::vector<std::pair<double,int> >
HMMPathPredictor::predictPath(int round, BasicBlockID bb, double confidence) const
{
  using namespace std;
  vector<pair<double,int> > output;
  vector<double> pvec = vd.getStateProbabilities(round - 1);
  vector<size_t> desc_ids = sort_indices_reverse(pvec);
  double accumulated_confidence = 0.0;

  // restrict to guide paths with initial basic block = bb
  // compute total probability
  int num_matches = 0;
  double match_probability_total = 0.0;
  for (size_t i = 0; i < fragment_medoids.size(); ++i) {
    shared_ptr<TrainingObject> frag_med = fragment_medoids[i];
    if (frag_med->trace.size() > 0 && frag_med->trace[0] == bb) {
      num_matches++;
      match_probability_total += pvec[i];
    }
  }
  // if no guide paths match, return empty output
  if (num_matches == 0) {
    return output;
  }

  // rescale confidence threshold
  confidence *= match_probability_total;

  for (size_t i = 0; i < desc_ids.size(); ++i) {
    // skip if bb does not match initial basic block
    size_t medoid_id = desc_ids[i];
    shared_ptr<TrainingObject> frag_med = fragment_medoids[medoid_id];
    if (frag_med->trace.size() == 0 || frag_med->trace[0] != bb)
      continue;
    double probability = pvec[medoid_id];
    output.push_back(pair<double,int>(probability, medoid_id));
    accumulated_confidence += probability;
    if (accumulated_confidence >= confidence)
      break;
  }
  return output;
}

////////////////////////////////////////////////////////////////////////////////

} // end namespace cliver

////////////////////////////////////////////////////////////////////////////////
