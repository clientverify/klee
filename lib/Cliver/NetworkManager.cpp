//===-- NetworkManager.cpp --------------------------------------*- C++ -*-===//
//
// <insert license>
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//

#include "NetworkManager.h"
#include "CVCommon.h"
#include "CVExecutor.h"
#include "CVExecutionState.h"
#include "ClientVerifier.h"

#include "llvm/Support/CommandLine.h"

#include "klee/Internal/Module/KInstruction.h"
#include "../Core/Executor.h"
#include "../Core/Memory.h"
#include "../Core/TimingSolver.h"

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

#define RETURN_SUCCESS_NO_SOCKET(action, retval) { \
	if (DebugNetworkManager) { \
	CVDEBUG("State: " << std::setw(4) << std::right << state_->id() \
			<< " - success - " << std::setw(8) << std::left << action);	} \
	executor->bind_local(target, state_, retval); \
	return; }

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
			<< " - failure - " << std::setw(8) << std::left << action << " - " \
			<< " socket " << file_descriptor << " doesn't exist");  } \
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

NetworkManager::NetworkManager(CVExecutionState* state) 
	: state_(state) {}

void NetworkManager::add_socket(const KTest* ktest) {
	sockets_.push_back(Socket(ktest));
}

void NetworkManager::add_socket(const SocketEventList &log) {
	sockets_.push_back(Socket(log));
}

void NetworkManager::add_socket(const SocketEvent &se, bool is_open) {
	sockets_.push_back(Socket(se,is_open));
}

void NetworkManager::clear_sockets() {
	sockets_.clear();
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

Socket* NetworkManager::socket(int fd) {
	if (!sockets_.empty()) {
		if (fd == -1) {
			return &sockets_.back();
		} else {
			for (unsigned i=0; i<sockets_.size(); i++) {
				if (sockets_[i].fd() == fd) {
					return &sockets_[i];
				}
			}
		}
	}
	return NULL;
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

////////////////////////////////////////////////////////////////////////////////

NetworkManagerTraining::NetworkManagerTraining(
		CVExecutionState* state) 
	: NetworkManager(state) {}

void NetworkManagerTraining::add_socket(const KTest* ktest) {
	cv_error("add_socket() not supported in training mode");
}

void NetworkManagerTraining::add_socket(const SocketEventList &log) {
	cv_error("add_socket() not supported in training mode");
}

NetworkManagerTraining* NetworkManagerTraining::clone(
		CVExecutionState *state) {
	NetworkManagerTraining* nwmt = new NetworkManagerTraining(*this);
	nwmt->state_ = state;
	return nwmt;
}

void NetworkManagerTraining::execute_open_socket(CVExecutor* executor,
		klee::KInstruction *target, int domain, int type, int protocol) {

	// During training, socket open always succeeds?
	RETURN_SUCCESS_NO_SOCKET("open", Socket::NextFileDescriptor);

}

void NetworkManagerTraining::execute_shutdown(CVExecutor* executor,
		klee::KInstruction *target, int fd, int how) {

	RETURN_FAILURE_NO_SOCKET("shutdown", "training mode");
}

////////////////////////////////////////////////////////////////////////////////

//NetworkManagerTrainingTetrinet::NetworkManagerTrainingTetrinet(
//		CVExecutionState* state) 
//	: NetworkManager(state) {}
//
//NetworkManagerTrainingTetrinet* NetworkManagerTrainingTetrinet::clone(
//		CVExecutionState *state) {
//	NetworkManagerTrainingTetrinet* nwmt 
//		= new NetworkManagerTrainingTetrinet(*this);
//	nwmt->state_ = state;
//	return nwmt;
//}
//
//void NetworkManagerTetrinet::execute_read(CVExecutor* executor,
//		klee::KInstruction *target, klee::ObjectState* object, int fd, int len) {
//
//	GET_SOCKET_OR_DIE_TRYIN("read", fd);
//
//	if (socket.type() != SocketEvent::RECV)
//		RETURN_FAILURE("read", "wrong type");
//
//	unsigned bytes_written = 0;
//
//	while (socket.has_data() && bytes_written < len) {
//		object->write8(bytes_written++, socket.next_byte());
//	}
//
//	if (socket.has_data()) {
//		//RETURN_FAILURE("read", "bytes remain len=" << len);
//	} else {
//		socket.advance();
//	}
//
//	RETURN_SUCCESS("read", bytes_written);
//}

////////////////////////////////////////////////////////////////////////////////

NetworkManager* NetworkManagerFactory::create(CVExecutionState* state) {
	switch (g_cliver_mode) {
		case DefaultMode: {
		case DefaultTrainingMode: 
		case VerifyWithTrainingPaths:
			NetworkManager *nm = new NetworkManager(state);
			foreach( SocketEventList *sel, g_client_verifier->socket_events()) {
				nm->add_socket(*sel);
			}
			return nm;
			break;
		}
		case TetrinetMode: {
			NetworkManagerTetrinet *nm = new NetworkManagerTetrinet(state);
			foreach( SocketEventList *sel, g_client_verifier->socket_events()) {
				nm->add_socket(*sel);
			}
			return nm;
	  }
		case OutOfOrderTrainingMode: {
			NetworkManagerTraining *nm 
				= new NetworkManagerTraining(state);
			return nm;
		}
		case TetrinetTrainingMode: 
			break;
	}
	cv_error("cliver mode not supported in NetworkManager");
	return NULL;
}

} // end namespace cliver
