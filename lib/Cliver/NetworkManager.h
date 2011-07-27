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

void ExternalHandler_network_read_event(
		klee::Executor* executor,
		klee::ExecutionState *state, 
		klee::KInstruction *target, 
    std::vector<klee::ref<klee::Expr> > &arguments);

class SocketEvent {
 public:
	enum Type { SEND, RECV };

	SocketEvent();
	Type type_;
	unsigned delta_;
	unsigned round_;
	unsigned length_;
	const uint8_t *data_;
};

class Socket {
 public:
	enum State { IDLE, READING, WRITING, FINISHED };
	Socket();
	unsigned delta() { return log_[id_]->delta_; }
	unsigned round() { return log_[id_]->round_; }
	unsigned length() { return log_[id_]->length_; }
	SocketEvent::Type type() { return log_[id_]->type_; }
	State state() { return state_; }
	void set_state(State state) { state_ = state; }
	void advance() { id_++; state_ = IDLE; offset_ = 0; }
	bool has_data() { return offset_ < log_[id_]->length_; }
	uint8_t next_byte() { return log_[id_]->data_[offset_++]; }
 private:
	const uint8_t* data() { return log_[id_]->data_ + offset_; }
	State state_;
	unsigned id_;
	unsigned offset_;
	std::vector<const SocketEvent* > log_;
};

class NetworkManager {
 public:

  NetworkManager(CVExecutionState* state);
	virtual void add_socket(const KTest* ktest);

	NetworkManager* clone(CVExecutionState *state);

	void handle_read_event(CVExecutionState &state,
		klee::KInstruction *target, klee::ref<klee::Expr> id_expr, 
		klee::ref<klee::Expr> address_expr, klee::ref<klee::Expr> len_expr);
 
	void execute_read(CVExecutor* executor, 
		klee::KInstruction *target, 
		klee::ObjectState* object, 
		int id, int len);


	CVExecutionState* state() { return state_; }
	unsigned round() { return round_; }

 private:
	unsigned round_;
	CVExecutionState *state_;
	std::map<int, Socket> sockets_;
};

class NetworkManagerFactory {
 public:
  static NetworkManager* create(CVExecutionState* state);
};

} // end namespace cliver
#endif // NETWORK_MANAGER_H
