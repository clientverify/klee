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

#include <algorithm>

namespace cliver {

bool DebugSocketFlag;
llvm::cl::opt<bool, true>
DebugSocket("debug-socket",
  llvm::cl::location(DebugSocketFlag),
  llvm::cl::init(false));

llvm::cl::opt<bool>
PrintAsciiSocket("print-ascii-socket",llvm::cl::init(false));

llvm::cl::opt<bool>
PrintOmitHeaders( "print-omit-headers",
  llvm::cl::desc("Print socket events without headers (default=false)"),
  llvm::cl::init(false));

int Socket::NextFileDescriptor = 10;

SocketEvent::SocketEvent(const KTestObject &object) {
  init(object.bytes, object.numBytes);
  set_type(object.name);
  set_header_length();

  // Extract timestamp
  timestamp =
      (1000000)*((uint64_t)object.timestamp.tv_sec)
      + (uint64_t)object.timestamp.tv_usec;
}

SocketEvent::SocketEvent(const unsigned char* buf, unsigned len) {
  init(buf, len);
}

// Initialization
void SocketEvent::init(const unsigned char* buf, unsigned len) {
  
  // Delta (Not used)
  delta = 0;

  // Set length
	length = len;
	
  if (ClientModelFlag == XPilot) {
		// Extract the client round number prefix
		client_round = (int)(((unsigned)buf[0] << 24) 
								| ((unsigned)buf[1] << 16) 
								| ((unsigned)buf[2] << 8) 
								| ((unsigned)buf[3]));
		buf += 4;
		length -= 4;
	} else {
		client_round = -1;
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

// Set the length of the header, depends on socket type and client type
void SocketEvent::set_header_length() {
  header_length = 0;
  if (ClientModelFlag == XPilot) {
    if (type == SocketEvent::SEND) {
      header_length = 4 + 4 + UBATOINT_I(data, 4);
    } else {
      header_length = 8;
    }
  }
}

void SocketEvent::print(std::ostream &os) const {
#define X(x) #x
	static std::string socketevent_types[] = { SOCKETEVENT_TYPES };
#undef X
	os << "[" << socketevent_types[type] << "][LEN:" << length << "]";
    if (ClientModelFlag == XPilot) {
      os << "[CLRN:" << client_round << "]";
      os << "[HLEN:" << header_length << "]";
      os << "[DELTA:" << delta << "]";
      os << " ";
    }

  if (DebugSocket) {
    if (PrintAsciiSocket) {
      std::string s(data.begin(), data.begin()+(length-1));
      os << "(ascii) " << s << ", (hex) ";
    }
    os << std::hex;
    unsigned i = PrintOmitHeaders ? header_length : 0;
    for (; i<length; ++i)
      os << (int)data[i] << ':';
    os << std::dec;
  }
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

	if (client_round < se.client_round)
		return true;

	if (length < se.length)
		return true;

	if (header_length < se.header_length)
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

bool SocketEventSizeLT::operator()(const SocketEvent* a, 
		const SocketEvent* b) const {
  return (a->data_size()) < b->data_size();
}

bool SocketEventLT::operator()(const SocketEvent* a, 
		const SocketEvent* b) const {
	return a->less(*b);
}

bool SocketEventDataOnlyLT::operator()(const SocketEvent* a, 
		const SocketEvent* b) const {
	//return a->data_less(*b);
  return std::lexicographical_compare(a->data.begin()+a->header_length, a->data.end(),
                                      b->data.begin()+b->header_length, b->data.end());
}

////////////////////////////////////////////////////////////////////////////////

Socket::Socket(const KTest *ktest)
    : file_descriptor_(Socket::NextFileDescriptor++), open_(false),
      end_reached_(false), state_(IDLE), index_(0), offset_(0),
      log_(new SocketEventList()),
      socket_source_(std::make_shared<SocketSourcePreloaded>(ktest)) {

  // Load first socket event if it exists.
  if (socket_source_->finished()) {
    end_reached_ = true; // degenerate case - empty log
  } else {
    log_->push_back(new SocketEvent(socket_source_->next())); // FIXME: leak
  }
}

Socket::Socket(const SocketEventList &log)
    : file_descriptor_(Socket::NextFileDescriptor++), open_(false),
      end_reached_(false), state_(IDLE), index_(0), offset_(0),
      log_(new SocketEventList()),
      socket_source_(std::make_shared<SocketSourcePreloaded>(log)) {

  // Load first socket event if it exists.
  if (socket_source_->finished()) {
    end_reached_ = true; // degenerate case - empty log
  } else {
    log_->push_back(new SocketEvent(socket_source_->next())); // FIXME: leak
  }
}

Socket::~Socket() {}

uint8_t Socket::next_byte() {
	assert(offset_ < event().length);
	return event().data[offset_++];
}

bool  Socket::has_data() {
 	return offset_ < event().length;
}

unsigned Socket::bytes_remaining() {
 	return event().length - offset_;
}

bool  Socket::is_open() {
	return open_ && !end_of_log();
}

bool  Socket::end_of_log() {
	return end_reached_;
}

void  Socket::open() {
	open_ = true;
}

void  Socket::set_state(State s) {
	state_ = s;
}

void  Socket::advance(){
  if (socket_source_->finished()) {
    end_reached_ = true;
    index_++; // This is necessary! Callers depend on one-past-the-end behavior.
  } else {
    log_->push_back(new SocketEvent(socket_source_->next())); // FIXME: leak
    index_++; // index_ points to last item in log_
  }
  state_ = IDLE;
  offset_ = 0;
}

const SocketEvent& Socket::event() {
  if (!(log_ && index_ < log_->size())) {
    cv_error("Socket::event() - invalid index into Socket log");
  }
  return *((*log_)[index_]);
}

const SocketEvent& Socket::previous_event() {
  if (!(log_ && index_ <= log_->size() && index_ > 0)) {
    cv_error("Socket::previous_event() - invalid index into Socket log");
  }
  return *((*log_)[index_ - 1]);
}

void Socket::print(std::ostream &os) {
#define X(x) #x
	static std::string socketevent_types[] = { SOCKETEVENT_TYPES };
	static std::string socket_states[] = { SOCKET_STATES };
#undef X
		
  os << "[ ";
	if (index_ < log_->size()) {

    if (ClientModelFlag == XPilot)
      os << "Client Round:" << client_round() << ", ";

		os << "Event: " << index_ << "/" << log_->size() << ", "
       //<< "Position: " << offset_ << "/" << event().length << ", "
			 << socket_states[state()] << ", " << socketevent_types[type()] << " ]";

		if (DebugSocket)
			 os << " " << event();

	} else {

		os << "Event: " << index_ << "/" << log_->size() << ", "
			 << socket_states[state()] << ", N/A ]";

	}
}

} // end namespace cliver
