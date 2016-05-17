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
#include <memory>

#define UBATOINT_I(_b,_i) \
    (((_b)[_i]<<24) + ((_b)[_i+1]<<16) + ((_b)[_i+2]<<8) + ((_b)[_i+3]))

#define UBATOINT(_b) UBATOINT_I(_b, 0)

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
	int client_round;
	unsigned length;
	unsigned header_length;
	uint64_t timestamp;
	std::vector<uint8_t> data;

	void print(std::ostream &os) const;
	bool equal(const SocketEvent &se) const;
	bool less(const SocketEvent &se) const;
	bool data_less(const SocketEvent &se) const;
  unsigned size() const { return length; }
  unsigned data_size() const { return length - header_length; }

 private:
  // Initialization
	void init(const unsigned char* buf, unsigned len);
	void set_type(const char* name);
  void set_header_length();

	// Serialization
	SocketEvent() {};
	friend class boost::serialization::access;
	template<class archive> 
	void serialize(archive & ar, const unsigned version) {
		ar & type;
		ar & delta;
		ar & client_round;
		ar & length;
		ar & header_length;
		ar & timestamp;
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

struct SocketEventSizeLT {
	bool operator()(const SocketEvent* a, const SocketEvent* b) const;
};

struct SocketEventDataOnlyLT {
	bool operator()(const SocketEvent* a, const SocketEvent* b) const;
};

typedef std::vector<const SocketEvent*> SocketEventList;

typedef std::set<SocketEvent*, SocketEventLT> SocketEventSet;
typedef std::set<SocketEvent*, SocketEventDataOnlyLT> SocketEventDataSet;

////////////////////////////////////////////////////////////////////////////////

class SocketSource {
public:
  virtual ~SocketSource() {}
  virtual bool finished() = 0;
  virtual const SocketEvent& next() = 0;
};

class SocketSourcePreloaded : public SocketSource {
public:
  SocketSourcePreloaded(const SocketEventList &log)
      : log_(new SocketEventList(log)), next_index_(0) {}
  SocketSourcePreloaded(const KTest *ktest) : next_index_(0) {
    SocketEventList *log = new SocketEventList();
    for (unsigned i = 0; i < ktest->numObjects; ++i) {
      log->push_back(new SocketEvent(ktest->objects[i])); // FIXME: memory leak
    }
    log_ = log;
  }
  virtual ~SocketSourcePreloaded() { delete log_; }
  virtual bool finished() { return next_index_ >= log_->size(); }
  virtual const SocketEvent &next() { return *((*log_)[next_index_++]); }

private:
  const SocketEventList *log_;
  size_t next_index_;
};

////////////////////////////////////////////////////////////////////////////////

// WARNING: a Socket can be copied, in which case different copies of
// the same Socket may be advance()'d separately and return a
// different index(), yet be working off of the same underlying
// SocketSource and SocketEventList.  Changes to Socket need to
// support this usage.
//
// This data structure is NOT thread-safe if advance() is called from
// different threads, since log_ and socket_source_ may be shared
// between copies of Socket objects.

class Socket {
 public:
	typedef enum { SOCKET_STATES } State;

	Socket(const KTest* ktest);
	Socket(const SocketEventList &log);
	~Socket();

  SocketEvent::Type type() { return event().type; }
  State state() { return state_; }
  unsigned length() { return event().length; }
  unsigned client_round() { return event().client_round; }
  int fd() { return file_descriptor_; }
  unsigned index() { return index_; }

  uint8_t next_byte();
  bool has_data();
  unsigned bytes_remaining();
  bool is_open();
  bool end_of_log();
  void open();
  void set_state(State s);
  void advance();

  void print(std::ostream &os);

  const SocketEvent &event();
  const SocketEvent &previous_event();

  static int NextFileDescriptor;

 protected:
	Socket() {}

	int file_descriptor_;
	bool open_;
	bool end_reached_;
	State state_;
	unsigned index_; // copies may point to different places in the log_
	unsigned offset_;
	SocketEventList  *log_; // socket events retrieved thus far (shared!)
  std::shared_ptr<SocketSource> socket_source_;
};

#undef X

inline std::ostream &operator<<(std::ostream &os, Socket &s) {
  s.print(os);
  return os;
}

} // end namespace cliver
#endif // CLIVER_SOCKET_H
