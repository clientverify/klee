//===-- NetworkManager.cpp --------------------------------------*- C++ -*-===//
//
// <insert license>
//
//===----------------------------------------------------------------------===//
//
// TODO: rewrite comments...
// TODO: make FAILURE/SUCCESS macros more readable
//
//===----------------------------------------------------------------------===//

#include "cliver/NetworkManager.h"
#include "cliver/CVAssignment.h"
#include "cliver/CVExecutor.h"
#include "cliver/CVExecutionState.h"
#include "cliver/CVStream.h"
#include "cliver/ClientVerifier.h"
#include "cliver/ExecutionObserver.h"
#include "cliver/ExecutionStateProperty.h"
#include "CVCommon.h"

#include "../Core/ImpliedValue.h"
#include "../Core/Memory.h"
#include "../Core/Common.h"
#include "../Core/TimingSolver.h"
#include "klee/Internal/Module/KInstruction.h"
#include "klee/util/ExprPPrinter.h"
#include "klee/util/ExprUtil.h"

#include "llvm/Support/CommandLine.h"

namespace cliver {

////////////////////////////////////////////////////////////////////////////////

extern bool DebugSocketFlag;
llvm::cl::opt<bool>
DebugNetworkManager("debug-network-manager",llvm::cl::init(false));

llvm::cl::opt<bool>
UseInPlaceConcretization("in-place-concretization",llvm::cl::init(false));

llvm::cl::opt<bool>
UseRecvProcessingFlag("use-recv-processing-flag",llvm::cl::init(false));

llvm::cl::opt<bool>
EnableXorOptimization("enable-xor-opt", llvm::cl::init(false));

////////////////////////////////////////////////////////////////////////////////

#ifndef NDEBUG

#undef CVDEBUG
#define CVDEBUG(x) \
	__CVDEBUG(DebugNetworkManager, x);

#undef CVDEBUG_S
#define CVDEBUG_S(__state_id, __x) \
	__CVDEBUG_S(DebugNetworkManager, __state_id, __x)

#define RETURN_FAILURE_NO_SOCKET(action, reason) { \
	if (DebugNetworkManager) { \
	CVDEBUG("State: " << std::setw(4) << std::right << state_->id() \
			<< " - failure - " << std::setw(8) << std::left << action << " - " \
			<< std::setw(15) << reason );	} \
	executor->terminate_state(state_); \
	return; }

#define RETURN_FAILURE(action, reason) \
	RETURN_FAILURE_NO_SOCKET(action, reason << " log:" << socket)

#define RETURN_FAILURE_OBJ(action, reason) \
	RETURN_FAILURE_NO_SOCKET(action, reason << " log:" << socket \
    << " sym:" << (DebugSocketFlag ? get_byte_string(object, len):"") )

#define RETURN_SUCCESS_NO_SOCKET(action, retval) { \
	if (DebugNetworkManager) { \
	CVDEBUG("State: " << std::setw(4) << std::right << state_->id() \
      << " ret:" << retval \
			<< " - success - " << std::setw(8) << std::left << action);	} \
	executor->bind_local(target, state_, retval); \
	return; }

#define RETURN_SUCCESS(action, retval) { \
	if (DebugNetworkManager) { \
	CVDEBUG("State: " << std::setw(4) << std::right << state_->id() \
      << " ret:" << retval \
			<< " - success - " << std::setw(8) << std::left << action);	} \
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

#undef CVDEBUG
#define CVDEBUG(x)

#undef CVDEBUG_S
#define CVDEBUG_S(__state_id, __x)

#define RETURN_FAILURE_NO_SOCKET(action, reason) { \
	executor->terminate_state(state_); \
	return; }

#define RETURN_FAILURE(action, reason) \
	RETURN_FAILURE_NO_SOCKET(action, reason)

#define RETURN_SUCCESS(action, retval) { \
	executor->bind_local(target, state_, retval); \
	return; }

#define RETURN_FAILURE_OBJ(action, reason) \
	RETURN_FAILURE_NO_SOCKET(action, reason)

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

NetworkManager::NetworkManager(CVExecutionState* state) 
	: state_(state) {}

void NetworkManager::add_socket(const KTest* ktest) {
	sockets_.push_back(Socket(ktest));
}

void NetworkManager::add_socket(const SocketEventList &log) {
	sockets_.push_back(Socket(log));
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

// FIXME: this is dangerous if the sockets_ list can be built
// incrementally with std::vector::push_back(), i.e., if a call to
// NetworkManager::add_socket() occurs between the call to
// NetworkManager::socket() and the use of the returned pointer.  If a
// reallocation happens, all iterators, pointers and references
// related to the container are invalidated.
// http://www.cplusplus.com/reference/vector/vector/push_back/)
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

  CVDEBUG("Opening first availible of " << sockets_.size() << " sockets");
  for (unsigned i = 0; i<sockets_.size(); ++i) {
		Socket &socket = sockets_[i];
		if (!socket.is_open()) {
			socket.open();
			RETURN_SUCCESS("open", socket.fd());
		} else {
      CVDEBUG("Socket " << i << " is not ready to be opened");
    }
	}

	RETURN_FAILURE_NO_SOCKET("open", "no socket availible");
}

std::string NetworkManager::get_byte_string(klee::ObjectState *obj, int len) {
  std::stringstream ss;
  for (int i=0; i<len; i++) {
    klee::ref<klee::Expr> e = obj->read8(i);
    if (klee::ConstantExpr *CE = dyn_cast<klee::ConstantExpr>(e)) {
      //int c = CE->getZExtValue();
      //if (c > 47 && c < 126)
      //  ss << (char)c << ":";
      //else
      //  ss << std::hex << c << ":";
      int c = CE->getZExtValue();
      ss << std::hex << std::setw(2) << std::setfill('0') << unsigned(c) << " ";
    } else {
      ss << e << " ";
    }
  }
  return ss.str();
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
		RETURN_FAILURE_OBJ("send", "not open");

	if (socket.type() != SocketEvent::SEND)
		RETURN_FAILURE_OBJ("send", "wrong type");

	if (socket.state() != Socket::IDLE)
		RETURN_FAILURE_OBJ("send", "wrong state");

	if (!socket.has_data() && len > 0)
		RETURN_FAILURE_OBJ("send", "no socket data");

	if ((int)socket.length() != len)
		RETURN_FAILURE_OBJ("send", "wrong length" << " " << socket.length() << " != " << len);

	klee::ref<klee::Expr> write_condition 
		= klee::ConstantExpr::alloc(1, klee::Expr::Bool);

	int bytes_read = 0;

  std::stringstream bytes_ss;
	while (socket.has_data() && bytes_read < len) {
    if (DebugNetworkManager) {
      bytes_ss << object->read8(bytes_read) << " ";
    }
		klee::ref<klee::Expr> condition
			= klee::EqExpr::create(
					object->read8(bytes_read++), 
					klee::ConstantExpr::alloc(socket.next_byte(), klee::Expr::Int8));
		write_condition = klee::AndExpr::create(write_condition, condition);
	}

	if (klee::ConstantExpr *CE = dyn_cast<klee::ConstantExpr>(write_condition)) {
		if (CE->isFalse())
			RETURN_FAILURE_OBJ("send", "not valid (1) ");
	} else {
		bool result; 

    if (EnableXorOptimization) {
      klee::ConstraintManager cm;
      write_condition = state_->constraints.simplifyWithXorOptimization(write_condition);
    }

    executor->compute_false(state_, write_condition, result);
    if (result)
			RETURN_FAILURE_OBJ("send", "not valid (2) ");

    if (DebugNetworkManager) {
      std::vector<const klee::Array*> arrays;
      klee::findSymbolicObjects(write_condition, arrays);
      std::ostringstream info;
      foreach (const klee::Array *arr, arrays) {
        info << arr->name << ", ";
      }
      CVDEBUG("Symbolic objects at network send: " << info.str());
    }
	}

	if (!socket.has_data()) {
    // Multi-pass: Clear old data
    state_->multi_pass_assignment().clear();

    // Multi-pass: Find unique solutions for symbolic variables
    if (!isa<klee::ConstantExpr>(write_condition))
      state_->multi_pass_assignment().solveForBindings(
          executor->get_solver()->solver, write_condition);


		socket.advance();
    state_->cv()->notify_all(ExecutionEvent(CV_SOCKET_ADVANCE, state_));
	} else {
		socket.set_state(Socket::WRITING);
		RETURN_FAILURE_OBJ("send", "no data left");
	}

  // Concretize variables now, explicitly, instead of implicitly
  // using another execution pass
  if (UseInPlaceConcretization) {
    klee::ImpliedValueList results;
    klee::ImpliedValue::getImpliedValues(
        write_condition,
        klee::ConstantExpr::alloc(1,klee::Expr::Bool),
        results);

    if (results.size()) {
      executor->doImpliedValueConcretization(
          *(static_cast<klee::ExecutionState*>(state_)),
          write_condition,
          klee::ConstantExpr::alloc(1,klee::Expr::Bool));
    }
  }

  CVDEBUG("Network Bytes: " << bytes_ss.str());

  if (!state_->multi_pass_assignment().bindings.size())
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

	if (socket.is_open() != true)
		RETURN_FAILURE("read", "not open");

	if (socket.type() != SocketEvent::RECV)
		RETURN_FAILURE("read", "wrong type");

	int bytes_written = 0;

	while (socket.has_data() && bytes_written < len) {
		object->write8(bytes_written++, socket.next_byte());
	}

	if (socket.has_data())
		RETURN_FAILURE("read", "bytes remain len=" << len);

	socket.advance();
  state_->cv()->notify_all(ExecutionEvent(CV_SOCKET_ADVANCE, state_));

  //state_->property()->is_recv_processing = true;
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

NetworkManagerXpilot::NetworkManagerXpilot(CVExecutionState* state) 
	: NetworkManager(state) {
}

NetworkManager* NetworkManagerXpilot::clone(CVExecutionState *state) {
	NetworkManagerXpilot* nwm = new NetworkManagerXpilot(*this);
	nwm->state_ = state;
	return nwm;
}

void NetworkManagerXpilot::execute_read(CVExecutor* executor,
		klee::KInstruction *target, klee::ObjectState* object, int fd, int len) {

	GET_SOCKET_OR_DIE_TRYIN("read", fd);

	int bytes_written = 0;
	
  if (socket.type() != SocketEvent::RECV)
    RETURN_SUCCESS("read-on-send", bytes_written);

	if (socket.client_round() != state()->property()->client_round)
    RETURN_SUCCESS("read-early", bytes_written);

	while (socket.has_data() && bytes_written < len) {
		object->write8(bytes_written++, socket.next_byte());
	}

	if (!socket.has_data()) {
		socket.advance();
    state_->cv()->notify_all(ExecutionEvent(CV_SOCKET_ADVANCE, state_));
  }

  if (UseRecvProcessingFlag)
    state_->property()->is_recv_processing = true;
  CVDEBUG("Currently recv processing");
	RETURN_SUCCESS("read", bytes_written);
}

void NetworkManagerXpilot::execute_write(CVExecutor* executor,
		klee::KInstruction *target, klee::ObjectState* object, int fd, int len) {

  //state_->property()->is_recv_processing = false;

	GET_SOCKET_OR_DIE_TRYIN("send", fd);

	if (socket.is_open() != true)
		RETURN_SUCCESS("send - not open", 0);

	if (socket.type() != SocketEvent::SEND)
		RETURN_FAILURE_OBJ("send", "wrong type");

	if (socket.state() != Socket::IDLE)
		RETURN_FAILURE_OBJ("send", "wrong state");

	if (socket.client_round() != state()->property()->client_round)
		RETURN_FAILURE_OBJ("send", "wrong round");

	if ((int)socket.length() != len)
		RETURN_FAILURE_OBJ("send", "wrong length" << " " << socket.length() << " != " << len);

	klee::ref<klee::Expr> write_condition 
		= klee::ConstantExpr::alloc(1, klee::Expr::Bool);

	int bytes_read = 0;

	while (socket.has_data() && bytes_read < len) {
		klee::ref<klee::Expr> condition
			= klee::EqExpr::create(
					object->read8(bytes_read++), 
					klee::ConstantExpr::alloc(socket.next_byte(), klee::Expr::Int8));
		write_condition = klee::AndExpr::create(write_condition, condition);
	}

	if (klee::ConstantExpr *CE = dyn_cast<klee::ConstantExpr>(write_condition)) {
		if (CE->isFalse())
			RETURN_FAILURE_OBJ("send", "not valid (1) ");
	} else {
		bool result; 
		executor->compute_false(state_, write_condition, result);
		if (result)
			RETURN_FAILURE_OBJ("send", "not valid (2) ");
	}

	if (!socket.has_data()) {
		socket.advance();
    state_->cv()->notify_all(ExecutionEvent(CV_SOCKET_ADVANCE, state_));
	} else {
		socket.set_state(Socket::WRITING);
		RETURN_FAILURE_OBJ("send", "no data left");
	}

	executor->add_constraint(state_, write_condition);
	RETURN_SUCCESS("send", bytes_read);
}

void NetworkManagerXpilot::execute_open_socket(CVExecutor* executor,
		klee::KInstruction *target, int domain, int type, int protocol) {

  for (unsigned i = 0; i<sockets_.size(); ++i) {
		Socket &socket = sockets_[i];
		if (!socket.is_open()) {
			socket.open();
			RETURN_SUCCESS("open", socket.fd());
		}
	}

  for (unsigned i = 0; i<sockets_.size(); ++i) {
		Socket &socket = sockets_[i];
		if (socket.is_open()) {
			RETURN_SUCCESS("open", socket.fd());
		}
	}

	RETURN_FAILURE_NO_SOCKET("open", "no socket availible");
}

////////////////////////////////////////////////////////////////////////////////

} // end namespace cliver
