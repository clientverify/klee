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
#include <string>
#include <sstream>
#include <cstring>
#include <stdexcept>

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
      socket_source_(std::make_shared<SocketSourcePreloaded>(ktest)),
      log_mutex_(std::make_shared<std::mutex>()) {

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
      socket_source_(std::make_shared<SocketSourcePreloaded>(log)),
      log_mutex_(std::make_shared<std::mutex>()) {

  // Load first socket event if it exists.
  if (socket_source_->finished()) {
    end_reached_ = true; // degenerate case - empty log
  } else {
    log_->push_back(new SocketEvent(socket_source_->next())); // FIXME: leak
  }
}

Socket::Socket(const std::string &ktest_text_file, bool drop_s2c_tls_appdata)
    : file_descriptor_(Socket::NextFileDescriptor++), open_(false),
      end_reached_(false), state_(IDLE), index_(0), offset_(0),
      log_(new SocketEventList()),
      socket_source_(std::make_shared<SocketSourceKTestText>(
          ktest_text_file, drop_s2c_tls_appdata)),
      log_mutex_(std::make_shared<std::mutex>()) {

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

void Socket::advance() {
  std::lock_guard<std::mutex> lock(*log_mutex_);
  state_ = IDLE;
  offset_ = 0;
  if (index_ + 1 < log_->size()) { // socket event already retrieved
    index_++;
  } else if (!socket_source_->finished()) { // must retrieve next socket event
    log_->push_back(new SocketEvent(socket_source_->next())); // FIXME: leak
    index_++;
  } else if (index_ + 1 == log_->size()) { // no more socket events
    index_ = log_->size(); // callers may depend on one-past-the-end index()
    end_reached_ = true;
  } else {
    cv_error("Socket::advance() called too many times");
  }
}

const SocketEvent& Socket::event() {
  std::lock_guard<std::mutex> lock(*log_mutex_);
  if (!(log_ && index_ < log_->size())) {
    cv_error("Socket::event() - invalid index into Socket log");
  }
  return *((*log_)[index_]);
}

const SocketEvent& Socket::previous_event() {
  std::lock_guard<std::mutex> lock(*log_mutex_);
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
		
  std::lock_guard<std::mutex> lock(*log_mutex_);
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

////////////////////////////////////////////////////////////////////////////////

/// SocketSourceKTestText Implementation

static std::vector<std::string> &split(const std::string &s, char delim,
                                       std::vector<std::string> &elems) {
  std::stringstream ss(s);
  std::string item;
  while (std::getline(ss, item, delim)) {
    if (!item.empty())
      elems.push_back(item);
  }
  return elems;
}

static std::vector<std::string> split(const std::string &s, char delim) {
  std::vector<std::string> elems;
  split(s, delim, elems);
  return elems;
}

static std::string strip_comments(const std::string &line) {
  size_t pos = line.find("#");

  if (pos == std::string::npos)
    return std::string(line);
  else
    return line.substr(0, pos);
}

static int char2int(char input) {
  if (input >= '0' && input <= '9')
    return input - '0';
  if (input >= 'A' && input <= 'F')
    return input - 'A' + 10;
  if (input >= 'a' && input <= 'f')
    return input - 'a' + 10;
  throw std::invalid_argument("Invalid input string");
}

// This function assumes src to be a zero terminated sanitized string with
// an even number of [0-9a-f] characters, and target to be sufficiently large
static void hex2bin(const char *src, unsigned char *target) {
  while (*src && src[1]) {
    *(target++) = char2int(*src) * 16 + char2int(src[1]);
    src += 2;
  }
}

static KTestObject *ktest_text_to_obj(const std::string &line) {
  KTestObject *obj = NULL;
  std::vector<std::string> fields = split(strip_comments(line), ' ');

  if (fields.size() < 4) {
    return NULL;
  }

  std::vector<std::string> time_parts = split(fields[0], '.');
  if (time_parts.size() != 2) {
    return NULL;
  }

  obj = new KTestObject();
  obj->name = new char[fields[2].size() + 1];
  std::strcpy(obj->name, fields[2].c_str());
  std::stringstream convert_sec(time_parts[0]);
  convert_sec >> (obj->timestamp.tv_sec);
  std::stringstream convert_usec(time_parts[1]);
  convert_usec >> (obj->timestamp.tv_usec);
  std::stringstream convert_numbytes(fields[3]);
  convert_numbytes >> (obj->numBytes);
  if (obj->numBytes > 0) {
    if (fields[4].size() != 2 * obj->numBytes) {
      delete[] obj->name;
      delete obj;
      return NULL;
    }
    obj->bytes = new unsigned char[obj->numBytes];
    hex2bin(fields[4].c_str(), obj->bytes);
  } else {
    obj->bytes = NULL;
  }

  return obj;
}

static KTestObject *get_next_ktest(std::ifstream &is) {
  while (is) {
    std::string line;
    std::getline(is, line);
    if (!is) {
      return NULL;
    }
    KTestObject *obj = ktest_text_to_obj(line);
    if (obj) {
      return obj;
    }
  }
  return NULL;
}

static void delete_KTestObject(KTestObject *obj) {
  if (obj) {
    if (obj->name)
      delete[] obj->name;
    if (obj->bytes)
      delete[] obj->bytes;
    delete obj;
  }
}

bool SocketSourceKTestText::finished() {
  // Definitely finished
  if (finished_)
    return true;

  // Definitely not finished
  if (index_ < log_.size())
    return false;

  // Not sure, have to check
  if (try_loading_next_ktest()) {
    return false; // not finished
  } else {
    finished_ = true;
    return true; // finished
  }
}

const SocketEvent &SocketSourceKTestText::next() {
  const SocketEvent &event = *(log_[index_++]);
  return event;
}

bool SocketSourceKTestText::try_loading_next_ktest() {
  while (is_) {
    KTestObject *obj = get_next_ktest(is_);
    if (obj) {
      if (drop_s2c_tls_appdata_ && is_s2c_tls_appdata(obj)) {
        delete_KTestObject(obj);
      } else {
        log_.push_back(new SocketEvent(*obj));
        delete_KTestObject(obj);
        return true;
      }
    }
  }
  return false;
}

// WARNING: stateful / side effects!
// s2c assumed to be split into header(5) + payload(n)
bool SocketSourceKTestText::is_s2c_tls_appdata(const KTestObject *obj) {
  const unsigned char TLS_APPDATA = 23; // RFC 5246
  if (!obj)
    return false;
  if (strcmp(obj->name, "s2c") != 0)
    return false;
  if (drop_next_s2c_) { // appdata payload
    drop_next_s2c_ = false;
    return true;
  }
  if (obj->numBytes == 5 && obj->bytes[0] == TLS_APPDATA) { // appdata header
    drop_next_s2c_ = true;
    return true;
  }
  return false; // some other kind of s2c message (e.g., handshake, alert)
}

} // end namespace cliver
