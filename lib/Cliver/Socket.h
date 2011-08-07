//===-- Socket.h ------------------------------------------------*- C++ -*-===//
//
// <insert license>
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//
#ifndef SOCKET_H
#define SOCKET_H 

#include "klee/Internal/ADT/KTest.h"
#include "CVStream.h"

#include <iostream>
#include <vector>

namespace cliver {

#define SOCKETEVENT_TYPES X(SEND), X(RECV) 
#define SOCKET_STATES     X(IDLE), X(READING), X(WRITING), X(FINISHED)
#define X(x) x

struct SocketEvent {
	SocketEvent(const KTestObject &object);
	typedef enum { SOCKETEVENT_TYPES } Type;
	Type type;
	unsigned delta;
	int round;
	unsigned length;
	const uint8_t *data;
};

typedef std::vector<const SocketEvent*> SocketEventList;

////////////////////////////////////////////////////////////////////////////////

class Socket {
 public:
	typedef enum { SOCKET_STATES } State;

	Socket(const KTest* ktest);
	Socket(const SocketEventList &log);

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

 protected:
	Socket() {}
	const SocketEvent& event();

	int file_descriptor_;
	bool open_;
	State state_;
	unsigned index_;
	unsigned offset_;
	const SocketEventList  *log_;
	static int NextFileDescriptor;
};

#undef X

inline std::ostream &operator<<(std::ostream &os, Socket &s) {
  s.print(os);
  return os;
}

} // end namespace cliver
#endif // SOCKET_H
