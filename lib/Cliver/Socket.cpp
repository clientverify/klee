//===-- Socket.cpp ----------------------------------------------*- C++ -*-===//
//
// <insert license>
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//

#include "Socket.h"
#include "ClientVerifier.h"

namespace cliver {

int Socket::NextFileDescriptor = 1000;

SocketEvent::SocketEvent(const KTestObject &object) {
	unsigned char *buf = object.bytes;
	
	if (g_cliver_mode == XpilotMode) {
		// Extract the round number prefix
		round = (int)(((unsigned)buf[0] << 24) 
								| ((unsigned)buf[1] << 16) 
								| ((unsigned)buf[2] << 8) 
								| ((unsigned)buf[3]));
		buf += 4;
	} else {
		round = -1;
	}

	data = buf;
	length = object.numBytes;

	// Set the type of the socket by using the Ktest object's name
	if (std::string(object.name) == "c2s") {
		type = SocketEvent::SEND;
	} else if (std::string(object.name) == "s2c") {
		type = SocketEvent::RECV;
	} else {
		cv_error("Invalid socket event name: \"%s\"", object.name);
	}
}

////////////////////////////////////////////////////////////////////////////////

Socket::Socket(const KTest* ktest) 
	: file_descriptor_(Socket::NextFileDescriptor++), 
	  open_(false), 
		state_(IDLE), 
		index_(0), 
		offset_(0) {

	SocketEventList *log = new SocketEventList();
	for (unsigned i=0; i<ktest->numObjects; ++i) {
		log->push_back(new SocketEvent(ktest->objects[i]));
	}
	log_ = log;
}

Socket::Socket(const SocketEventList &log) 
	: file_descriptor_(Socket::NextFileDescriptor++), 
	  open_(false), 
		state_(IDLE), 
		index_(0), 
		offset_(0),
		log_(new SocketEventList(log)) {
}

uint8_t Socket::next_byte() {
	assert(offset_ < event().length);
	return event().data[offset_++];
}

bool  Socket::has_data() {
 	return offset_ < event().length;
}

bool  Socket::is_open() {
	return open_ && (index_ < log_->size());
}

void  Socket::open() {
	open_ = true;
}

void  Socket::set_state(State s) {
	state_ = s;
}

void  Socket::advance(){ 
	index_++; state_ = IDLE; offset_ = 0;
}

const SocketEvent& Socket::event() { 
	assert (index_ < log_->size());
	return *((*log_)[index_]);
}

void Socket::print(std::ostream &os) {
#define X(x) #x
	static std::string socketevent_types[] = { SOCKETEVENT_TYPES };
	static std::string socket_states[] = { SOCKET_STATES };
#undef X
		
	if (index_ < log_->size()) {
		os << "[ "
			 //<< "Round:" << round() ", "
			 << "Event: " << index_ << "/" << log_->size() << ", "
			 << socket_states[state()] << ", " << socketevent_types[type()] << " ]";
	} else {
		os << "[ "
			 //<< "Round:" << round() ", "
			 << "Event: " << index_ << "/" << log_->size() << ", "
			 << socket_states[state()] << ", N/A ]";
	}
}

} // end namespace cliver
