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
	int events_remaining()   { return log_.size() - id_; }
	int log_size() 					 { return log_.size(); }
	unsigned id() 					 { return id_; }
	unsigned length()				 { return event().length; }

	uint8_t next_byte();
	bool has_data();
	bool is_open();
	void open();
	void set_state(State s);
	void advance();
	void add_event(SocketEvent* e);

  void print(std::ostream &os);

 private:
	Socket() {}
	const SocketEvent& event();

	bool open_;
	State state_;
	unsigned id_;
	unsigned offset_;
	std::vector<const SocketEvent* > log_;
};

#undef X

inline std::ostream &operator<<(std::ostream &os, Socket &s) {
  s.print(os);
  return os;
}


class NetworkManager {
 typedef std::map< int, Socket > SocketMap;
 typedef std::pair< int, Socket > SocketPair;
 public:

  NetworkManager(CVExecutionState* state);
	void add_socket(const KTest* ktest);

	NetworkManager* clone(CVExecutionState *state);

	void execute_open_socket(CVExecutor* executor,
		klee::KInstruction *target, 
		int domain, int type, int protocol);

	void execute_read(CVExecutor* executor, 
		klee::KInstruction *target, 
		klee::ObjectState* object, int id, int len);

	void execute_write(CVExecutor* executor,
		klee::KInstruction *target, 
		klee::ObjectState* object, int id, int len);

	void execute_shutdown(CVExecutor* executor,
		klee::KInstruction *target, 
		int id, int how);

	CVExecutionState* state() { return state_; }
	unsigned round() { return round_; }

 private:
	unsigned round_;
	CVExecutionState *state_;
	SocketMap sockets_;
};

class NetworkManagerFactory {
 public:
  static NetworkManager* create(CVExecutionState* state);
};

} // end namespace cliver
#endif // NETWORK_MANAGER_H
