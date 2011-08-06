//===-- NetworkManager.cpp --------------------------------------*- C++ -*-===//
//
// <insert license>
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//

#include "NetworkManager.h"
#include "ClientVerifier.h"
#include "llvm/Support/CommandLine.h"
#include "../Core/Memory.h"
#include "../Core/TimingSolver.h"

#include <iomanip>

#include <boost/foreach.hpp>
#define foreach BOOST_FOREACH 

namespace cliver {

llvm::cl::opt<bool>
DebugNetworkManager("debug-network-manager",llvm::cl::init(false));

#ifndef NDEBUG

#define RETURN_FAILURE_NO_SOCKET(action, reason) { \
	if (DebugNetworkManager) { \
	CVDEBUG("State: " << std::setw(4) << std::right << state_->id() \
			<< " - failure - " << std::setw(8) << std::left << action << " - " \
			<< std::setw(15) << reason );	} \
	executor->terminate_state(state_); \
	return; }

#define RETURN_FAILURE(action, reason) \
	RETURN_FAILURE_NO_SOCKET(action, reason << " " << socket)

#define RETURN_SUCCESS(action, retval) { \
	if (DebugNetworkManager) { \
	CVDEBUG("State: " << std::setw(4) << std::right << state_->id() \
			<< " - success - " << std::setw(8) << std::left << action << "   " \
			<< std::setw(15) << " " << socket);	} \
	executor->bind_local(target, state_, retval); \
	return; }

#define GET_SOCKET_OR_DIE_TRYIN(action, file_descriptor) \
	unsigned socket_index; \
  for (socket_index = 0; socket_index < sockets_.size(); ++socket_index) \
		if (file_descriptor == sockets_[socket_index].fd()) \
			break; \
  if (socket_index == sockets_.size()) { \
		if (DebugNetworkManager) { \
		CVDEBUG("State: " << std::setw(4) << std::right << state_->id() \
				<< " - failure - socket " << file_descriptor << " doesn't exist");  } \
		executor->terminate_state(state_); \
		return; \
	} \
	Socket &socket = sockets_[socket_index]; 

#else

#define RETURN_FAILURE_NO_SOCKET(action, reason) { \
	executor->terminate_state(state_); \
	return; }

#define RETURN_FAILURE(action, reason) \
	RETURN_FAILURE_NO_SOCKET(action, reason)

#define RETURN_SUCCESS(action, retval) { \
	executor->bind_local(target, state_, retval); \
	return; }

#define GET_SOCKET_OR_DIE_TRYIN(action, file_descriptor) \
	unsigned socket_index; \
  for (socket_index = 0; socket_index < sockets_.size(); ++socket_index) \
		if (file_descriptor == sockets_[socket_index].fd()) \
			break; \
  if (socket_index == sockets_.size()) { \
		executor->terminate_state(state_); \
		return; \
	} \
	Socket &socket = sockets_[socket_index]; 

#endif

////////////////////////////////////////////////////////////////////////////////

const int kInitialFileDescriptor= 1000;

klee::ObjectState* resolve_address(klee::Executor* executor, 
		klee::ExecutionState* state, klee::ref<klee::Expr> address) {
	klee::ObjectPair result;
	static_cast<CVExecutor*>(executor)->resolve_one(state, address, result);
	return const_cast<klee::ObjectState*>(result.second);
}

void ExternalHandler_socket_create(
		klee::Executor* executor, klee::ExecutionState *state, 
		klee::KInstruction *target, std::vector<klee::ref<klee::Expr> > &arguments) {

  int domain 	 = cast<klee::ConstantExpr>(arguments[0])->getZExtValue();
  int type     = cast<klee::ConstantExpr>(arguments[1])->getZExtValue();
  int protocol = cast<klee::ConstantExpr>(arguments[2])->getZExtValue();

	CVExecutionState* cv_state = static_cast<CVExecutionState*>(state);
	CVExecutor *cv_executor = static_cast<CVExecutor*>(executor);
	cv_state->network_manager()->execute_open_socket(cv_executor, target, 
			domain, type, protocol);
}

void ExternalHandler_socket_read(
		klee::Executor* executor, klee::ExecutionState *state, 
		klee::KInstruction *target, std::vector<klee::ref<klee::Expr> > &arguments) {
	assert(arguments.size() >= 3);

  int fd = cast<klee::ConstantExpr>(arguments[0])->getZExtValue();
	klee::ref<klee::Expr> address = arguments[1];
  int len = cast<klee::ConstantExpr>(arguments[2])->getZExtValue();
	klee::ObjectState *object = resolve_address(executor, state, address);

	CVExecutionState* cv_state = static_cast<CVExecutionState*>(state);
	CVExecutor *cv_executor = static_cast<CVExecutor*>(executor);
	cv_state->network_manager()->execute_read(cv_executor, target, object, fd, len);
}

void ExternalHandler_socket_write(
		klee::Executor* executor, klee::ExecutionState *state, 
		klee::KInstruction *target, std::vector<klee::ref<klee::Expr> > &arguments) {
	assert(arguments.size() >= 3);

  int fd = cast<klee::ConstantExpr>(arguments[0])->getZExtValue();
	klee::ref<klee::Expr> address = arguments[1];
  int len = cast<klee::ConstantExpr>(arguments[2])->getZExtValue();
	klee::ObjectState *object = resolve_address(executor, state, address);

	CVExecutionState* cv_state = static_cast<CVExecutionState*>(state);
	CVExecutor *cv_executor = static_cast<CVExecutor*>(executor);
	cv_state->network_manager()->execute_write(cv_executor, target, object, fd, len);
}

void ExternalHandler_socket_shutdown(
		klee::Executor* executor, klee::ExecutionState *state, 
		klee::KInstruction *target, std::vector<klee::ref<klee::Expr> > &arguments) {
	assert(arguments.size() >= 2);

  int fd  = cast<klee::ConstantExpr>(arguments[0])->getZExtValue();
  int how = cast<klee::ConstantExpr>(arguments[1])->getZExtValue();

	CVExecutionState* cv_state = static_cast<CVExecutionState*>(state);
	CVExecutor *cv_executor = static_cast<CVExecutor*>(executor);
	cv_state->network_manager()->execute_shutdown(cv_executor, target, fd, how);
}

////////////////////////////////////////////////////////////////////////////////

Socket::Socket(const KTest* ktest) 
	: file_descriptor_(-1), open_(false), state_(IDLE), index_(0), offset_(0) {
	
	static int fd = kInitialFileDescriptor;
	file_descriptor_ = fd++;
	std::vector<const SocketEvent*> *log = new std::vector<const SocketEvent*>();
	
	// Convert the convert the data in the given Ktest struct into SocketEvents. 
	for (unsigned i=0; i<ktest->numObjects; ++i) {
		SocketEvent* event = new SocketEvent();
		unsigned char *buf = ktest->objects[i].bytes;
		
		// Extract the round number prefix
		//event->round = (int)(((unsigned)buf[0] << 24) 
		//		               | ((unsigned)buf[1] << 16) 
		//									 | ((unsigned)buf[2] << 8) 
		//									 | ((unsigned)buf[3]));
		//buf += 4;
		event->data = buf;
		event->length = ktest->objects[i].numBytes;

		// Set the type of the socket by using the Ktest object's name
		if (std::string(ktest->objects[i].name) == "c2s")
			event->type = SocketEvent::SEND;
		else if (std::string(ktest->objects[i].name) == "s2c")
			event->type = SocketEvent::RECV;
		else 
			cv_error("Invalid socket event name: \"%s\"", ktest->objects[i].name);

		log->push_back(event);
	}
	log_ = log;
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

////////////////////////////////////////////////////////////////////////////////

NetworkManager::NetworkManager(CVExecutionState* state) 
	: state_(state) {}

void NetworkManager::add_socket(const KTest* ktest) {
	sockets_.push_back(Socket(ktest));
}

NetworkManager* NetworkManager::clone(CVExecutionState *state) {
	NetworkManager* nwm = new NetworkManager(*this);
	nwm->state_ = state;
	return nwm;
}

int NetworkManager::socket_log_index(int fd) {
	if (sockets_.size() > 0) {
		if (fd == -1) {
			return sockets_.back().index();
		} else {
			foreach (Socket s, sockets_) {
				if (s.fd() == fd) {
					return s.index();
				}
			}
		}
	}
	return -1;
}

NetworkManager* NetworkManagerFactory::create(CVExecutionState* state) {
  switch (g_cliver_mode) {
	case DefaultMode:
    break;
	case TetrinetMode: 
		return new NetworkManagerTetrinet(state);
  }
  return new NetworkManager(state);
}

void NetworkManager::execute_open_socket(CVExecutor* executor,
		klee::KInstruction *target, int domain, int type, int protocol) {

  for (unsigned i = 0; i<sockets_.size(); ++i) {
		Socket &socket = sockets_[i];
		if (!socket.is_open()) {
			socket.open();
			CVDEBUG("opened socket " << socket.fd());
			RETURN_SUCCESS("open", socket.fd());
		}
	}

	RETURN_FAILURE_NO_SOCKET("open", "no socket availible");
}

/* 
 * Client action for each log type on a Send in the ith round:
 *    LogRecv_i       terminate
 *    LogRecv_i+1     terminate
 *    LogSend_i       add constraint 
 *    LogSend_i+1     terminate
 */
void NetworkManager::execute_write(CVExecutor* executor,
		klee::KInstruction *target, klee::ObjectState* object, int fd, int len) {

	GET_SOCKET_OR_DIE_TRYIN("send", fd);

	if (socket.is_open() != true)
		RETURN_FAILURE("send", "not open");

	if (socket.type() != SocketEvent::SEND)
		RETURN_FAILURE("send", "wrong type");

	if (socket.state() != Socket::IDLE)
		RETURN_FAILURE("send", "wrong state");

	if (socket.length() != len)
		RETURN_FAILURE("send", "wrong length");

	klee::ref<klee::Expr> write_condition 
		= klee::ConstantExpr::alloc(1, klee::Expr::Bool);

	unsigned bytes_read = 0;

	while (socket.has_data() && bytes_read < len) {
		klee::ref<klee::Expr> condition
			= klee::EqExpr::create(
					object->read8(bytes_read++), 
					klee::ConstantExpr::alloc(socket.next_byte(), klee::Expr::Int8));
		write_condition = klee::AndExpr::create(write_condition, condition);
	}

	if (klee::ConstantExpr *CE = dyn_cast<klee::ConstantExpr>(write_condition)) {
		if (CE->isFalse())
			RETURN_FAILURE("send", "not valid");
	} else {
		bool result; 
		executor->compute_truth(state_, write_condition, result);
		if (!result)
			RETURN_FAILURE("send", "not valid");
	}

	if (!socket.has_data()) {
		socket.advance();
	} else {
		socket.set_state(Socket::WRITING);
		RETURN_FAILURE("send", "no data left");
	}

	executor->add_constraint(state_, write_condition);
	RETURN_SUCCESS("send", bytes_read);
}

/*
 * Socket Read/Write Semantics:
 *
 * Client action for each log type on a Recv in the ith round:
 *    LogRecv_i       return message
 *    LogRecv_i+1     return 0 
 *    LogSend_i       return 0 
 *    LogSend_i+1     return 0 
 *
 * Although the log buffer is technically a stream of bytes, it is broken up into
 * discrete chunks to simulate the passage of time between messages. If the provided
 * receive buffer is not large enough hold the current LogBuffer. We copy as much
 * as possible each time the socket is read until all has been copied. When all bytes
 * have been copied we return 0 and advance the LogBuffer index.
 *
 * Semantics of nuklear socket read: Up to len bytes of obj are copied into
 * the caller's buffer at the given address. If len < logBufSize, the
 * next attempt to read this socket will return up to len remain bytes of
 * obj, and so on, until all logBufSize have been given to caller, at
 * which point the next attempt to read the socket will return 0 and the
 * index variable will be incremented.
 *
*/
void NetworkManager::execute_read(CVExecutor* executor,
		klee::KInstruction *target, klee::ObjectState* object, int fd, int len) {

	GET_SOCKET_OR_DIE_TRYIN("read", fd);

	if (socket.type() != SocketEvent::RECV)
		RETURN_FAILURE("read", "wrong type");

	unsigned bytes_written = 0;

	while (socket.has_data() && bytes_written < len) {
		object->write8(bytes_written++, socket.next_byte());
	}

	if (socket.has_data())
		RETURN_FAILURE("read", "bytes remain len=" << len);

	socket.advance();
	RETURN_SUCCESS("read", bytes_written);
}

void NetworkManager::execute_shutdown(CVExecutor* executor,
		klee::KInstruction *target, int fd, int how) {

	GET_SOCKET_OR_DIE_TRYIN("shutdown", fd);

	if (socket.is_open())
		RETURN_FAILURE("shutdown", "events remain");

	RETURN_SUCCESS("shutdown", 0);
}

////////////////////////////////////////////////////////////////////////////////

NetworkManagerTetrinet::NetworkManagerTetrinet(CVExecutionState* state) 
	: NetworkManager(state) {
}

NetworkManager* NetworkManagerTetrinet::clone(CVExecutionState *state) {
	NetworkManagerTetrinet* nwm = new NetworkManagerTetrinet(*this);
	nwm->state_ = state;
	return nwm;
}

void NetworkManagerTetrinet::execute_read(CVExecutor* executor,
		klee::KInstruction *target, klee::ObjectState* object, int fd, int len) {

	GET_SOCKET_OR_DIE_TRYIN("read", fd);

	if (socket.type() != SocketEvent::RECV)
		RETURN_FAILURE("read", "wrong type");

	unsigned bytes_written = 0;

	while (socket.has_data() && bytes_written < len) {
		object->write8(bytes_written++, socket.next_byte());
	}

	if (socket.has_data()) {
		//RETURN_FAILURE("read", "bytes remain len=" << len);
	} else {
		socket.advance();
	}

	RETURN_SUCCESS("read", bytes_written);
}


} // end namespace cliver
