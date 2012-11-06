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
#include "cliver/CVStream.h"
#include "cliver/EditDistance.h"
#include <iostream>
#include <boost/regex.hpp>
#include <boost/lexical_cast.hpp>

////////////////////////////////////////////////////////////////////////////////
#include <string>

/* before version 3.8.0 this was 8 bytes. */
#define KEYBOARD_SIZE		9

#define DEBRIS_TYPES    (8 * 4 * 4)

/*
 * Definition of various client/server packet types.
 */

/* packet types: 0 - 9 */
#define PKT_UNDEFINED		0
#define PKT_VERIFY		1
#define PKT_REPLY		2
#define PKT_PLAY		3
#define PKT_QUIT		4
#define PKT_MESSAGE		5
#define PKT_START		6
#define PKT_END			7
#define PKT_SELF		8
#define PKT_DAMAGED		9

/* packet types: 10 - 19 */
#define PKT_CONNECTOR		10
#define PKT_REFUEL		11
#define PKT_SHIP		12
#define PKT_ECM			13
#define PKT_PAUSED		14
#define PKT_ITEM		15
#define PKT_MINE		16
#define PKT_BALL		17
#define PKT_MISSILE		18
#define PKT_SHUTDOWN		19

/* packet types: 20 - 29 */
#define PKT_STRING		20
#define PKT_DESTRUCT		21
#define PKT_RADAR		22
#define PKT_TARGET		23
#define PKT_KEYBOARD		24
#define PKT_SEEK		25
#define PKT_SELF_ITEMS		26	/* still under development */
#define PKT_TEAM_SCORE		27	/* was PKT_SEND_BUFSIZE */
#define PKT_PLAYER		28
#define PKT_SCORE		29

/* packet types: 30 - 39 */
#define PKT_FUEL		30
#define PKT_BASE		31
#define PKT_CANNON		32
#define PKT_LEAVE		33
#define PKT_POWER		34
#define PKT_POWER_S		35
#define PKT_TURNSPEED		36
#define PKT_TURNSPEED_S		37
#define PKT_TURNRESISTANCE	38
#define PKT_TURNRESISTANCE_S	39

/* packet types: 40 - 49 */
#define PKT_WAR			40
#define PKT_MAGIC		41
#define PKT_RELIABLE		42
#define PKT_ACK			43
#define PKT_FASTRADAR		44
#define PKT_TRANS		45
#define PKT_ACK_CANNON		46
#define PKT_ACK_FUEL		47
#define PKT_ACK_TARGET		48
#define	PKT_SCORE_OBJECT	49

/* packet types: 50 - 59 */
#define PKT_AUDIO		50
#define PKT_TALK		51
#define PKT_TALK_ACK		52
#define PKT_TIME_LEFT		53
#define PKT_LASER		54
#define PKT_DISPLAY		55
#define PKT_EYES		56
#define PKT_SHAPE		57
#define PKT_MOTD		58
#define PKT_LOSEITEM		59

/* packet types: 60 - 69 */
#define PKT_APPEARING		60
#define PKT_TEAM		61
#define PKT_POLYSTYLE		62
#define PKT_ACK_POLYSTYLE	63
#define PKT_NOT_USED_64		64
#define PKT_NOT_USED_65		65
#define PKT_NOT_USED_66		66
#define PKT_NOT_USED_67		67
#define PKT_MODIFIERS		68
#define PKT_FASTSHOT		69	/* replaces SHOT/TEAMSHOT */

/* packet types: 70 - 79 */
#define PKT_THRUSTTIME		70
#define PKT_MODIFIERBANK	71
#define PKT_SHIELDTIME		72
#define PKT_POINTER_MOVE	73
#define PKT_REQUEST_AUDIO	74
#define PKT_ASYNC_FPS		75
#define PKT_TIMING		76
#define PKT_PHASINGTIME		77
#define PKT_ROUNDDELAY		78
#define PKT_WRECKAGE		79

/* packet types: 80 - 89 */
#define PKT_ASTEROID		80
#define PKT_WORMHOLE		81
#define PKT_NOT_USED_82		82
#define PKT_NOT_USED_83		83
#define PKT_NOT_USED_84		84
#define PKT_NOT_USED_85		85
#define PKT_NOT_USED_86		86
#define PKT_NOT_USED_87		87
#define PKT_NOT_USED_88		88
#define PKT_NOT_USED_89		89

/* packet types: 90 - 99 */
/*
 * Use these 10 packet type numbers for
 * experimenting with new packet types.
 */

/* status reports: 101 - 102 */
#define PKT_FAILURE		101
#define PKT_SUCCESS		102

/* optimized packet types: 128 - 255 */
#define PKT_DEBRIS		128		/* + color + x + y */

#if 0
std::string xpilot_packet_string(int type)
{

    if (type > PKT_DEBRIS && type < PKT_DEBRIS+DEBRIS_TYPES)
      type = PKT_DEBRIS;

    switch(type) {
    case PKT_EYES:	return std::string("PKT_EYES"); 		
    case PKT_TIME_LEFT:	return std::string("PKT_TIME_LEFT"); 	
    case PKT_AUDIO:	return std::string("PKT_AUDIO"); 		
    case PKT_START:	return std::string("PKT_START"); 		
    case PKT_END:	return std::string("PKT_END"); 		
    case PKT_SELF:	return std::string("PKT_SELF"); 		
    case PKT_DAMAGED:	return std::string("PKT_DAMAGED"); 		
    case PKT_CONNECTOR:	return std::string("PKT_CONNECTOR"); 	
    case PKT_LASER:	return std::string("PKT_LASER"); 		
    case PKT_REFUEL:	return std::string("PKT_REFUEL"); 		
    case PKT_SHIP:	return std::string("PKT_SHIP"); 		
    case PKT_ECM:	return std::string("PKT_ECM"); 		
    case PKT_TRANS:	return std::string("PKT_TRANS"); 		
    case PKT_PAUSED:	return std::string("PKT_PAUSED"); 		
    case PKT_APPEARING:	return std::string("PKT_APPEARING"); 	
    case PKT_ITEM:	return std::string("PKT_ITEM"); 		
    case PKT_MINE:	return std::string("PKT_MINE"); 		
    case PKT_BALL:	return std::string("PKT_BALL"); 		
    case PKT_MISSILE:	return std::string("PKT_MISSILE"); 		
    case PKT_SHUTDOWN:	return std::string("PKT_SHUTDOWN"); 	
    case PKT_DESTRUCT:	return std::string("PKT_DESTRUCT"); 	
    case PKT_SELF_ITEMS:return std::string("PKT_SELF_ITEMS"); 	
    case PKT_FUEL:	return std::string("PKT_FUEL");		
    case PKT_CANNON:	return std::string("PKT_CANNON"); 		
    case PKT_TARGET:	return std::string("PKT_TARGET"); 		
    case PKT_RADAR:	return std::string("PKT_RADAR"); 		
    case PKT_FASTRADAR:	return std::string("PKT_FASTRADAR"); 	
    case PKT_RELIABLE:	return std::string("PKT_RELIABLE"); 	
    case PKT_QUIT:	return std::string("PKT_QUIT"); 		
    case PKT_MODIFIERS:	return std::string("PKT_MODIFIERS"); 	
    case PKT_FASTSHOT:	return std::string("PKT_FASTSHOT"); 	
    case PKT_THRUSTTIME:return std::string("PKT_THRUSTTIME"); 	
    case PKT_SHIELDTIME:return std::string("PKT_SHIELDTIME"); 	
    case PKT_PHASINGTIME:return std::string("PKT_PHASINGTIME"); 	
    case PKT_ROUNDDELAY:return std::string("PKT_ROUNDDELAY"); 	
    case PKT_LOSEITEM:	return std::string("PKT_LOSEITEM"); 	
    case PKT_WRECKAGE:	return std::string("PKT_WRECKAGE"); 	
    case PKT_ASTEROID:	return std::string("PKT_ASTEROID"); 	
    case PKT_WORMHOLE:	return std::string("PKT_WORMHOLE"); 	
    case PKT_POLYSTYLE:	return std::string("PKT_POLYSTYLE"); 	
    case PKT_DEBRIS:	return std::string("PKT_DEBRIS"); 		
    /* reliable types */
    case PKT_MOTD:	return std::string("PKT_MOTD"); 		
    case PKT_MESSAGE:	return std::string("PKT_MESSAGE");		
    case PKT_TEAM_SCORE:return std::string("PKT_TEAM_SCORE"); 	
    case PKT_PLAYER:	return std::string("PKT_PLAYER");		
    case PKT_TEAM:	return std::string("PKT_TEAM"); 		
    case PKT_SCORE:	return std::string("PKT_SCORE"); 		
    case PKT_TIMING:	return std::string("PKT_TIMING");		
    case PKT_LEAVE:	return std::string("PKT_LEAVE"); 		
    case PKT_WAR:	return std::string("PKT_WAR"); 		
    case PKT_SEEK:	return std::string("PKT_SEEK"); 		
    case PKT_BASE:	return std::string("PKT_BASE"); 		
    case PKT_STRING:	return std::string("PKT_STRING");		
    case PKT_SCORE_OBJECT:return std::string("PKT_SCORE_OBJECT"); 	
    case PKT_TALK_ACK:	return std::string("PKT_TALK_ACK");		
    case PKT_ACK:	return std::string("PKT_ACK");		
    case PKT_KEYBOARD: return std::string("PKT_KEYBOARD");		
    case PKT_VERIFY: return std::string("PKT_VERIFY");		
    case PKT_SHAPE: return std::string("PKT_SHAPE");		
    case PKT_DISPLAY: return std::string("PKT_DISPLAY");		
    case PKT_ACK_POLYSTYLE:	return std::string("PKT_ACK_POLYSTYLE");		
    default:{
      std::stringstream ss;
      ss << "TYPE_" << type;
      return ss.str();
      //return std::string("DEFAULT");
    }
    }
}
#endif

////////////////////////////////////////////////////////////////////////////////

namespace cliver {

class SocketEvent;

/// TODO
class SocketEventSimilarity {
 public:
  virtual int similarity_score(const SocketEvent* a, const SocketEvent* b) {
    if (a->data.size() != b->data.size())
      return INT_MAX;

    for (unsigned i=0; i<a->data.size(); ++i) {
      if (a->data[i] != b->data[i]) {
        return INT_MAX;
      }
    }
    return 0;
  }
};

/// TODO
class SocketEventSimilarityXpilotEqual : public SocketEventSimilarity {
 public:
  int similarity_score(const SocketEvent* a, const SocketEvent* b) {
    int result = 0;

    if (a->type != b->type) {
      return INT_MAX;
    }

    if (a->type == SocketEvent::SEND) {
      
      //int32_t kbseq_a, kbseq_b, acklen_a, acklen_b, start_a, start_b;

      //kbseq_a = UBATOINT_I(a->data, 0);
      //kbseq_b = UBATOINT_I(b->data, 0);

      //acklen_a = UBATOINT_I(a->data, 4);
      //acklen_b = UBATOINT_I(b->data, 4);

      //start_a = 4 + 4 + acklen_a;
      //start_b = 4 + 4 + acklen_b;

      ////std::cout << "a & b are SEND events: "
      ////    << "a: " << xpilot_packet_string(a->data[start_a])
      ////    << ", b: " << xpilot_packet_string(b->data[start_b])
      ////    << ", size_a: " << a->data.size() << ", size_b: " << b->data.size()
      ////    << ", start_a: " << start_a  << ", start_b: " << start_b
      ////    << ", seq_a: " << kbseq_a  << ", seq_b: " << kbseq_b
      ////    << ", acklen_a: " << acklen_a << ", acklen_b: " << acklen_b << std::endl;

      //assert(acklen_a < a->data.size());
      //assert(acklen_b < b->data.size());

      ////for (unsigned i=start_a; i<a->data.size(); ++i)
      ////  std::cout << (int)( a->data[i]) << ", ";
      ////std::cout << std::endl;
      ////for (unsigned i=start_b; i<b->data.size(); ++i)
      ////  std::cout << (int)(b->data[i]) << ", ";
      ////std::cout << std::endl;
 
      //std::vector<uint8_t> msg_a(a->data.begin()+start_a, a->data.end());
      //std::vector<uint8_t> msg_b(b->data.begin()+start_b, b->data.end());
      
      std::vector<uint8_t> msg_a(a->data.begin()+a->header_length, a->data.end());
      std::vector<uint8_t> msg_b(b->data.begin()+b->header_length, b->data.end());

      if (msg_a != msg_b)
        result = INT_MAX;


    } else if (a->type == SocketEvent::RECV) {
      // data is 8 bytes in
      
      //int32_t smseq_a, smseq_b, kbseq_a, kbseq_b;
      //int32_t start_a, start_b;
      ////for (unsigned i=0; i<8; ++i)
      ////  std::cout << (int)( a->data[i]) << ", ";
      ////std::cout << std::endl;
      ////for (unsigned i=0; i<8; ++i)
      ////  std::cout << (int)(b->data[i]) << ", ";
      ////std::cout << std::endl;

      //smseq_a = UBATOINT_I(a->data, 0);
      //smseq_b = UBATOINT_I(b->data, 0);
      //kbseq_a = UBATOINT_I(a->data, 4);
      //kbseq_b = UBATOINT_I(b->data, 4);

      //start_a = 4 + 4;
      //start_b = 4 + 4;

      ////std::cout << "a & b are RECV events: "
      ////  << "a: " << xpilot_packet_string(a->data[start_a+13])
      ////  << ", b: " << xpilot_packet_string(b->data[start_b+13]) << ", "
      ////  << smseq_a << ", " << smseq_b << ", "
      ////  << kbseq_a << ", " << kbseq_b << std::endl;
 
      //std::vector<uint8_t> msg_a(a->data.begin()+start_a, a->data.end());
      //std::vector<uint8_t> msg_b(b->data.begin()+start_b, b->data.end());

      std::vector<uint8_t> msg_a(a->data.begin()+a->header_length, a->data.end());
      std::vector<uint8_t> msg_b(b->data.begin()+b->header_length, b->data.end());

      if (msg_a != msg_b)
        result = INT_MAX;

    }

    return result;
  }

};

typedef Score<std::vector<uint8_t>, uint8_t, int> UCharVecScore;
typedef EditDistanceRowIt<UCharVecScore, std::vector<uint8_t>, int > UCharVecEditDistance;

class SocketEventSimilarityDataOnly : public SocketEventSimilarity {
 public:
  int similarity_score(const SocketEvent* a, const SocketEvent* b) {

    if (a->type != b->type)
      return INT_MAX;

    UCharVecEditDistance ed(a->data_size(), b->data_size());
    std::vector<uint8_t>::const_iterator a_begin = a->data.begin();
    std::advance(a_begin, a->header_length);
    std::vector<uint8_t>::const_iterator b_begin = b->data.begin();
    std::advance(b_begin, b->header_length);
    int dist = ed.compute_editdistance(a_begin, b_begin);
    return dist;
};

/// TODO
class SocketEventSimilarityXpilot : public SocketEventSimilarity {
 public:
  int similarity_score(const SocketEvent* a, const SocketEvent* b) {
    int result = 0.0f;

    if (a->type != b->type) {
      return INT_MAX;
    }

    if (a->type == SocketEvent::SEND) {
      
      //int32_t kbseq_a, kbseq_b, acklen_a, acklen_b, start_a, start_b;

      //kbseq_a = UBATOINT_I(a->data, 0);
      //kbseq_b = UBATOINT_I(b->data, 0);

      //acklen_a = UBATOINT_I(a->data, 4);
      //acklen_b = UBATOINT_I(b->data, 4);

      //start_a = 4 + 4 + acklen_a;
      //start_b = 4 + 4 + acklen_b;

      ////std::cout << "a & b are SEND events: "
      ////    << "a: " << xpilot_packet_string(a->data[start_a])
      ////    << ", b: " << xpilot_packet_string(b->data[start_b])
      ////    << ", size_a: " << a->data.size() << ", size_b: " << b->data.size()
      ////    << ", start_a: " << start_a  << ", start_b: " << start_b
      ////    << ", seq_a: " << kbseq_a  << ", seq_b: " << kbseq_b
      ////    << ", acklen_a: " << acklen_a << ", acklen_b: " << acklen_b << std::endl;

      //assert(acklen_a < a->data.size());
      //assert(acklen_b < b->data.size());

      ////for (unsigned i=start_a; i<a->data.size(); ++i)
      ////  std::cout << (int)( a->data[i]) << ", ";
      ////std::cout << std::endl;
      ////for (unsigned i=start_b; i<b->data.size(); ++i)
      ////  std::cout << (int)(b->data[i]) << ", ";
      ////std::cout << std::endl;
 
      //std::vector<uint8_t> msg_a(a->data.begin()+start_a, a->data.end());
      //std::vector<uint8_t> msg_b(b->data.begin()+start_b, b->data.end());

      //std::vector<uint8_t> msg_a(a->data.begin()+a->header_length, a->data.end());
      //std::vector<uint8_t> msg_b(b->data.begin()+b->header_length, b->data.end());

      //UCharVecEditDistance ed(msg_a, msg_b);
      //int val = ed.compute_editdistance();
      //result = val;

      //result = (double)(val) / (double)(std::max(msg_a.size(), msg_b.size()));
      //result = (double)(val);
      //CVMESSAGE("Edit distance(S) = " << result);

      UCharVecEditDistance ed(a->data_size(), b->data_size());
      std::vector<uint8_t>::const_iterator a_begin = a->data.begin();
      std::advance(a_begin, a->header_length);
      std::vector<uint8_t>::const_iterator b_begin = b->data.begin();
      std::advance(b_begin, b->header_length);
      int val = ed.compute_editdistance(a_begin, b_begin);
      result = val;
     
    } else if (a->type == SocketEvent::RECV) {
      // data is 8 bytes in
      
      //int32_t smseq_a, smseq_b, kbseq_a, kbseq_b;
      //int32_t start_a, start_b;
      ////for (unsigned i=0; i<8; ++i)
      ////  std::cout << (int)( a->data[i]) << ", ";
      ////std::cout << std::endl;
      ////for (unsigned i=0; i<8; ++i)
      ////  std::cout << (int)(b->data[i]) << ", ";
      ////std::cout << std::endl;

      //smseq_a = UBATOINT_I(a->data, 0);
      //smseq_b = UBATOINT_I(b->data, 0);
      //kbseq_a = UBATOINT_I(a->data, 4);
      //kbseq_b = UBATOINT_I(b->data, 4);

      //start_a = 4 + 4;
      //start_b = 4 + 4;

      ////std::cout << "a & b are RECV events: "
      ////  << "a: " << xpilot_packet_string(a->data[start_a+13])
      ////  << ", b: " << xpilot_packet_string(b->data[start_b+13]) << ", "
      ////  << smseq_a << ", " << smseq_b << ", "
      ////  << kbseq_a << ", " << kbseq_b << std::endl;
 
      //std::vector<uint8_t> msg_a(a->data.begin()+start_a, a->data.end());
      //std::vector<uint8_t> msg_b(b->data.begin()+start_b, b->data.end());

      //std::vector<uint8_t> msg_a(a->data.begin()+a->header_length, a->data.end());
      //std::vector<uint8_t> msg_b(b->data.begin()+b->header_length, b->data.end());

      ////if (msg_a != msg_b)
      ////  result += 1.0f;
      //UCharVecEditDistance ed(msg_a, msg_b);
      //int val = ed.compute_editdistance();
      //result = val;
      //result = (double)(val) / (double)(std::max(msg_a.size(), msg_b.size()));
      //result = (double)(val);
      //CVMESSAGE("Edit distance(R) = " << result);

      UCharVecEditDistance ed(a->data_size(), b->data_size());
      std::vector<uint8_t>::const_iterator a_begin = a->data.begin();
      std::advance(a_begin, a->header_length);
      std::vector<uint8_t>::const_iterator b_begin = b->data.begin();
      std::advance(b_begin, b->header_length);
      int val = ed.compute_editdistance(a_begin, b_begin);
      result = val;

    }
    return result;
  }

};


class SocketEventSimilarityTetrinet: public SocketEventSimilarity {
 public:

  // Constructor
  SocketEventSimilarityTetrinet() {
    packet_type_regex_ 
        = boost::regex("^([a-zA-Z0-9]+).*$");
    player_move_regex_ 
        = boost::regex("^p ([0-9]+) ([0-9]+) ([0-9]+) ([0-9]+)$");
  }

  // Return a 0 to 1.0 measure of how similar two SocketEvents are, 0.0 being
  // equal, 1.0 being very different
  int similarity_score(const SocketEvent* a, const SocketEvent* b) {
    int result = 0;

    assert(a->data[a->data.size()-1] == 0xFF);
    assert(b->data[b->data.size()-1] == 0xFF);

    // Initialize strings, skip last element (non-ascii 0xFF)
    std::string str_a(a->data.begin(), a->data.begin()+a->data.size()-1);
    std::string str_b(b->data.begin(), b->data.begin()+b->data.size()-1);
    //std::cout << "comparing: " << str_a << " with " << str_b << std::endl;

    // Check if the packet types are equal
    //result += check_packet_type(str_a, str_b);
    if(!check_packet_type(str_a, str_b))
      return INT_MAX;

    // If the packet type is a player move, calculate the difference
    //result += check_player_move(str_a, str_b);
    result = check_player_move(str_a, str_b);
    if (result == INT_MAX) 
      return 0;
    return result;
  }

 private:

  // Extract packet type name, and compare if equal
  bool check_packet_type(std::string &a, std::string &b) {
    boost::smatch what_a, what_b;
    if (regex_match(a, what_a, packet_type_regex_) && 
        regex_match(b, what_b, packet_type_regex_) &&
        what_a[1].str() == what_b[1].str()) {
      //std::cout << "ptypes: " 
      //    << what_a[1].str() << " and " << what_b[1].str() << std::endl;
      return true;
    }
    return false;
  }

  // Compute difference if packet is of type player move 
  int check_player_move(std::string &a, std::string &b) {
    boost::smatch what_a, what_b;
    if (regex_match(a, what_a, player_move_regex_) && 
        regex_match(b, what_b, player_move_regex_)) {

      int val_a, val_b, sum_of_differences = 0;

      for (int i=2; i<5; ++i) {
        val_a = boost::lexical_cast<int>(what_a[i].str());
        val_b = boost::lexical_cast<int>(what_b[i].str());
        sum_of_differences += std::abs(val_a - val_b);
      }
      return sum_of_differences;

      //double max_difference = 12 + 20 + 3; // TODO correct ?
      //assert(sum_of_differences < max_difference);
      //if (sum_of_differences > 0)
      //  return (double)sum_of_differences / max_difference;
    }
    return INT_MAX;
  }

  boost::regex packet_type_regex_; 
  boost::regex player_move_regex_;

};
////////////////////////////////////////////////////////////////////////////////

class ClientVerifier;

class SocketEventSimilarityFactory {
 public:
  static SocketEventSimilarity* create();
};

////////////////////////////////////////////////////////////////////////////////

} // end namespace cliver

#endif // CLIVER_SOCKET_EVENT_MEASUREMENT_H

