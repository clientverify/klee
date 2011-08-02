//===-- NetworkManager.h ----------------------------------------*- C++ -*-===//
//
// <insert license>
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//
#ifndef NETWORK_MANAGER_H
#define NETWORK_MANAGER_H

#include "klee/Internal/Module/KInstruction.h"
#include "klee/Internal/ADT/KTest.h"
#include "../Core/Executor.h"
#include "CVExecutionState.h"
#include "CVExecutor.h"
#include "CVStream.h"

namespace cliver {

void ExternalHandler_socket_create(
	klee::Executor* executor, klee::ExecutionState *state, 
	klee::KInstruction *target, std::vector<klee::ref<klee::Expr> > &arguments);

void ExternalHandler_socket_read(
	klee::Executor* executor, klee::ExecutionState *state, 
	klee::KInstruction *target, std::vector<klee::ref<klee::Expr> > &arguments);

void ExternalHandler_socket_write(
	klee::Executor* executor, klee::ExecutionState *state, 
	klee::KInstruction *target, std::vector<klee::ref<klee::Expr> > &arguments);

void ExternalHandler_socket_shutdown(
		klee::Executor* executor, klee::ExecutionState *state, 
		klee::KInstruction *target, std::vector<klee::ref<klee::Expr> > &arguments);

////////////////////////////////////////////////////////////////////////////////

#define SOCKETEVENT_TYPES X(SEND), X(RECV) 
#define SOCKET_STATES     X(IDLE), X(READING), X(WRITING), X(FINISHED)
#define X(x) x

struct SocketEvent {
	typedef enum { SOCKETEVENT_TYPES } Type;
	Type type;
	unsigned delta;
	int round;
	unsigned length;
	const uint8_t *data;
};

class Socket {
 public:
	typedef enum { SOCKET_STATES } State;

	Socket(const KTest* ktest);

	unsigned delta() 				 { return event().delta; }
	int round()      				 { return event().round; }
	SocketEvent::Type type() { return event().type; }
	State state() 					 { return state_; }
	int events_remaining()   { return log_.size() - index_; }
	int log_size() 					 { return log_.size(); }
	unsigned index()				 { return index_; }
	unsigned length()				 { return event().length; }
	int fd() 								 { return file_descriptor_; }

	uint8_t next_byte();
	bool has_data();
	bool is_open();
	void open();
	void set_state(State s);
	void advance();
	void add_event(SocketEvent* e);

  void print(std::ostream &os);

 protected:
	Socket() {}
	const SocketEvent& event();

	int file_descriptor_;
	bool open_;
	State state_;
	unsigned index_;
	unsigned offset_;
	std::vector<const SocketEvent* > log_;
};

#undef X

inline std::ostream &operator<<(std::ostream &os, Socket &s) {
  s.print(os);
  return os;
}

////////////////////////////////////////////////////////////////////////////////

class NetworkManager {
 public:

  NetworkManager(CVExecutionState* state);

	virtual void add_socket(const KTest* ktest);

	virtual NetworkManager* clone(CVExecutionState *state);

	virtual void execute_open_socket(CVExecutor* executor,
		klee::KInstruction *target, 
		int domain, int type, int protocol);

	virtual void execute_read(CVExecutor* executor, 
		klee::KInstruction *target, 
		klee::ObjectState* object, int fd, int len);

	virtual void execute_write(CVExecutor* executor,
		klee::KInstruction *target, 
		klee::ObjectState* object, int fd, int len);

	virtual void execute_shutdown(CVExecutor* executor,
		klee::KInstruction *target, 
		int fd, int how);

	CVExecutionState* state() { return state_; }
	unsigned round() { return round_; }
	std::vector<Socket>& sockets() { return sockets_; }

 protected:
	unsigned round_;
	CVExecutionState *state_;
	std::vector<Socket> sockets_;
};

class NetworkManagerFactory {
 public:
  static NetworkManager* create(CVExecutionState* state);
};

////////////////////////////////////////////////////////////////////////////////

class NetworkManagerTetrinet : public NetworkManager {
 public:

  NetworkManagerTetrinet(CVExecutionState* state);

	virtual NetworkManager* clone(CVExecutionState *state);

	virtual void execute_read(CVExecutor* executor, 
		klee::KInstruction *target, 
		klee::ObjectState* object, int fd, int len);
};

////////////////////////////////////////////////////////////////////////////////

} // end namespace cliver
#endif // NETWORK_MANAGER_H
