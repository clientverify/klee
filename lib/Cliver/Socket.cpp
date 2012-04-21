//===-- Socket.cpp ----------------------------------------------*- C++ -*-===//
//
// <insert license>
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//

#include "cliver/Socket.h"
#include "cliver/ClientVerifier.h"
#include "cliver/CVStream.h"
#include "CVCommon.h"

#include "llvm/Support/CommandLine.h"

namespace cliver {

bool DebugSocketFlag;
llvm::cl::opt<bool, true>
DebugSocket("debug-socket",llvm::cl::location(DebugSocketFlag),llvm::cl::init(false));

llvm::cl::opt<bool>
XpilotSocket("xpilot-socket",llvm::cl::init(false));

int Socket::NextFileDescriptor = 1000;

SocketEvent::SocketEvent(const KTestObject &object) {
  init(object.bytes, object.numBytes);
  set_type(object.name);
}

SocketEvent::SocketEvent(const unsigned char* buf, unsigned len) {
  init(buf, len);
}

// Initialization
void SocketEvent::init(const unsigned char* buf, unsigned len) {

  // Set length
	length = len;
	
	if (XpilotSocket) {
		// Extract the round number prefix
		round = (int)(((unsigned)buf[0] << 24) 
								| ((unsigned)buf[1] << 16) 
								| ((unsigned)buf[2] << 8) 
								| ((unsigned)buf[3]));
		buf += 4;
		length -= 4;
	} else {
		round = -1;
	}

  // Set socket data bytes
	data = std::vector<uint8_t>(buf, buf+length);
}

// Set the type of the socket by using the Ktest object's name
void SocketEvent::set_type(const char* name) {
	if (std::string(name) == "c2s") {
		type = SocketEvent::SEND;
	} else if (std::string(name) == "s2c") {
		type = SocketEvent::RECV;
	} else {
		cv_error("Invalid socket event name: \"%s\"", name);
	}
}

void SocketEvent::print(std::ostream &os) const {
#define X(x) #x
	static std::string socketevent_types[] = { SOCKETEVENT_TYPES };
#undef X
	os << "[" << socketevent_types[type] << "][LEN:" << length << "]";
  if (XpilotSocket)
    os << "[RN:" << round << "] ";
	for (unsigned i=0; i<length; ++i) {
    if (data[i] > 47 && data[i] < 126) {
      os << (char)(data[i]) << ":";
    } else {
      char buf[8];
      sprintf(buf,"%X", data[i]);
      //os << std::hex << data[i];
      os << buf << ":";
    }
	}
	os << std::dec;
}

bool SocketEvent::equal(const SocketEvent &se) const {
	if (!less(se) && !se.less(*this))
		return true;
	return false;
}

bool SocketEvent::less(const SocketEvent &se) const {
	if (type < se.type)
		return true;

	if (delta < se.delta)
		return true;

	if (round < se.round)
		return true;

	if (length < se.length)
		return true;

	if (data_less(se))
		return true;

	return false;
}

bool SocketEvent::data_less(const SocketEvent &se) const {
	for (unsigned i=0; i<length; ++i) {
		if (data[i] != se.data[i]) {
			if (data[i] < se.data[i])
				return true;
			else
				return false;
		}
	}
	return false;
}

bool SocketEventLT::operator()(const SocketEvent* a, 
		const SocketEvent* b) const {
	return a->less(*b);
}

bool SocketEventDataOnlyLT::operator()(const SocketEvent* a, 
		const SocketEvent* b) const {
	return a->data_less(*b);
}

////////////////////////////////////////////////////////////////////////////////

Socket::Socket(const KTest* ktest) 
	: file_descriptor_(Socket::NextFileDescriptor++), 
	  open_(false), 
		state_(IDLE), 
		index_(0), 
		offset_(0),
		event_(NULL) {

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
		log_(new SocketEventList(log)),
		event_(NULL) {
}

Socket::Socket(const SocketEvent &se, bool is_open) 
	: file_descriptor_(Socket::NextFileDescriptor), 
	  open_(is_open), 
		state_(IDLE), 
		index_(0), 
		offset_(0),
		log_(NULL),
		event_(&se) {}

Socket::~Socket() {}

uint8_t Socket::next_byte() {
	assert(offset_ < event().length);
	return event().data[offset_++];
}

bool  Socket::has_data() {
 	return offset_ < event().length;
}

bool  Socket::is_open() {
	if (event_) {
		if (index_ != 0) cv_error (" index is not zero %d", index_);
		assert(index_ == 0);
		return open_;
	}
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
	if (event_) return *event_;
	assert (log_ && index_ < log_->size());
	return *((*log_)[index_]);
}

const SocketEvent& Socket::previous_event(){ 
	// ::previous_event() not supported when using single event Socket
	if (event_) cv_error("previous_event not supported");
	assert (log_ && index_ <= log_->size() && index_ > 0);
	return *((*log_)[index_-1]);
}

void Socket::print(std::ostream &os) {
#define X(x) #x
	static std::string socketevent_types[] = { SOCKETEVENT_TYPES };
	static std::string socket_states[] = { SOCKET_STATES };
#undef X
		
	if (event_) {
		os << "[ "
			 //<< "Round:" << round() ", "
			 << "Event: " << index_ << "/" << 1 << ", "
			 << socket_states[state()] << ", " << socketevent_types[type()] << " ]";
		if (DebugSocket)
			 os << " " << event();
	} else if (index_ < log_->size()) {
		os << "[ "
			 //<< "Round:" << round() ", "
			 << "Event: " << index_ << "/" << log_->size() << ", "
			 << socket_states[state()] << ", " << socketevent_types[type()] << " ]";
		if (DebugSocket)
			 os << " " << event();
	} else {
		os << "[ "
			 //<< "Round:" << round() ", "
			 << "Event: " << index_ << "/" << log_->size() << ", "
			 << socket_states[state()] << ", N/A ]";
	}
}

} // end namespace cliver
