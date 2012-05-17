//===-- Socket.h ------------------------------------------------*- C++ -*-===//
//
// <insert license>
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//
#ifndef CLIVER_SOCKET_H
#define CLIVER_SOCKET_H 

#include "klee/Internal/ADT/KTest.h"
#include "stdint.h"

#include <iostream>
#include <vector>
#include <set>

#include <boost/serialization/access.hpp>

namespace cliver {

#define SOCKETEVENT_TYPES X(SEND), X(RECV) 
#define SOCKET_STATES     X(IDLE), X(READING), X(WRITING), X(FINISHED)
#define X(x) x

class SocketEvent {
 public:
	SocketEvent(const KTestObject &object);
  SocketEvent(const unsigned char* buf, unsigned len);

	typedef enum { SOCKETEVENT_TYPES } Type;
	Type type;
	unsigned delta;
	int round;
	unsigned length;
	std::vector<uint8_t> data;

	void print(std::ostream &os) const;
	bool equal(const SocketEvent &se) const;
	bool less(const SocketEvent &se) const;
	bool data_less(const SocketEvent &se) const;
  unsigned size() const { return length; }

 private:
  // Initialization
	void init(const unsigned char* buf, unsigned len);
	void set_type(const char* name);

	// Serialization
	SocketEvent() {};
	friend class boost::serialization::access;
	template<class archive> 
	void serialize(archive & ar, const unsigned version) {
		ar & type;
		ar & delta;
		ar & round;
		ar & length;
		ar & data;
	}
};

inline std::ostream &operator<<(std::ostream &os, const SocketEvent &se) {
  se.print(os);
  return os;
}

struct SocketEventLT {
	bool operator()(const SocketEvent* a, const SocketEvent* b) const;
};

struct SocketEventDataOnlyLT {
	bool operator()(const SocketEvent* a, const SocketEvent* b) const;
};

typedef std::vector<const SocketEvent*> SocketEventList;

typedef std::set<SocketEvent*, SocketEventLT> SocketEventSet;
typedef std::set<SocketEvent*, SocketEventDataOnlyLT> SocketEventDataSet;

////////////////////////////////////////////////////////////////////////////////

class Socket {
 public:
	typedef enum { SOCKET_STATES } State;

	Socket(const KTest* ktest);
	Socket(const SocketEventList &log);
  Socket(const SocketEvent &se, bool is_open);
	~Socket();

	SocketEvent::Type type() { return event().type; }
	State state() 					 { return state_; }
	unsigned length()				 { return event().length; }
	unsigned round()				 { return event().round; }
	int fd() 								 { return file_descriptor_; }
	unsigned index()				 { return index_; }

	uint8_t next_byte();
	bool has_data();
	bool is_open();
	void open();
	void set_state(State s);
	void advance();

  void print(std::ostream &os);

	const SocketEvent& event();
	const SocketEvent& last_event();
	const SocketEvent& previous_event();

	static int NextFileDescriptor;
 protected:
	Socket() {}

	int file_descriptor_;
	bool open_;
	State state_;
	unsigned index_;
	unsigned offset_;
	const SocketEventList  *log_;
	const SocketEvent *event_;
};

#undef X

inline std::ostream &operator<<(std::ostream &os, Socket &s) {
  s.print(os);
  return os;
}

} // end namespace cliver
#endif // CLIVER_SOCKET_H
