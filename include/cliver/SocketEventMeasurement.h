//===-- SocketEventMeasurement.h --------------------------------*- C++ -*-===//
//
// <insert license>
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//
#ifndef CLIVER_SOCKET_EVENT_MEASUREMENT_H
#define CLIVER_SOCKET_EVENT_MEASUREMENT_H

#include "cliver/Socket.h"
#include <iostream>
#include <boost/regex.hpp>
#include <boost/lexical_cast.hpp>

////////////////////////////////////////////////////////////////////////////////

namespace cliver {

class SocketEvent;

/// TODO
class SocketEventSimilarity {
 public:
  virtual double similarity_score(const SocketEvent* a, const SocketEvent* b) = 0;
};

/// TODO
class SocketEventSimilarityXpilot : public SocketEventSimilarity {
 public:
  double similarity_score(const SocketEvent* a, const SocketEvent* b) {
    return 0;
  }
};

class SocketEventSimilarityTetrinet: public SocketEventSimilarity {
 public:

  // Constructor
  SocketEventSimilarityTetrinet() {
    packet_type_regex_ 
        = boost::regex("^([a-zA-Z]+) .*$");
    player_move_regex_ 
        = boost::regex("^p ([0-9]+) ([0-9]+) ([0-9]+) ([0-9]+)$");
  }

  // Return a 0 to 1.0 measure of how similar two SocketEvents are, 0.0 being
  // equal, 1.0 being very different
  double similarity_score(const SocketEvent* a, const SocketEvent* b) {
    double result = 0.0f;

    // Initialize strings
    std::string str_a(a->data.begin(), a->data.end());
    std::string str_b(b->data.begin(), b->data.end());

    // Check if the packet types are equal
    result += check_packet_type(str_a, str_b);

    // If the packet type is a player move, calculate the difference
    result += check_player_move(str_a, str_b);

    return result;
  }

 private:

  // Extract packet type name, and compare if equal
  double check_packet_type(std::string &a, std::string &b) {
    boost::smatch what_a, what_b;
    if (regex_match(a, what_a, packet_type_regex_) && 
        regex_match(b, what_b, packet_type_regex_) &&
        what_a[1].str() == what_b[1].str())
      return 0.0f;
    return 1.0f;
  }

  // Compute difference if packet is of type player move 
  double check_player_move(std::string &a, std::string &b) {
    boost::smatch what_a, what_b;
    if (regex_match(a, what_a, player_move_regex_) && 
        regex_match(b, what_b, player_move_regex_)) {

      int val_a, val_b, sum_of_differences = 0;

      for (int i=2; i<5; ++i) {
        val_a = boost::lexical_cast<int>(what_a[i].str());
        val_b = boost::lexical_cast<int>(what_b[i].str());
        sum_of_differences += std::abs(val_a - val_b);
      }

      double max_difference = 12 + 20 + 3; // TODO correct ?
      assert(sum_of_differences < max_difference);
      if (sum_of_differences > 0)
        return max_difference / (double)sum_of_differences;
    }
    return 0.0;
  }

  boost::regex packet_type_regex_; 
  boost::regex player_move_regex_;

};

////////////////////////////////////////////////////////////////////////////////

} // end namespace cliver

#endif // CLIVER_SOCKET_EVENT_MEASUREMENT_H

