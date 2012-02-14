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

#include "klee/Expr.h"
#include "klee/Internal/ADT/KTest.h"
#include "cliver/CVExecutionState.h"
#include "cliver/Socket.h"

namespace klee {
	class KInstruction;
	class ExecutionState;
	class Executor;
}

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

void ExternalHandler_merge(
		klee::Executor* executor, klee::ExecutionState *state, 
		klee::KInstruction *target, std::vector<klee::ref<klee::Expr> > &arguments);

void ExternalHandler_XEventsQueued(
		klee::Executor* executor, klee::ExecutionState *state, 
		klee::KInstruction *target, std::vector<klee::ref<klee::Expr> > &arguments);
////////////////////////////////////////////////////////////////////////////////

class CVExecutor;
class CVExecutionState;
class ClientVerifier;

class NetworkManager {
 public:

  NetworkManager(CVExecutionState* state);

	virtual void add_socket(const KTest* ktest);
	virtual void add_socket(const SocketEventList &log);
	//virtual void add_socket(const SocketEvent &se, bool is_open = true);
	virtual void clear_sockets();

	virtual NetworkManager* clone(CVExecutionState *state);

	virtual int socket_log_index(int fd=-1);

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


  std::string get_byte_string(klee::ObjectState *obj, int len);

	CVExecutionState* state() { return state_; }
	//unsigned round() { return round_; }
	std::vector<Socket>& sockets() { return sockets_; }
	Socket* socket(int fd=-1);

 protected:
	//unsigned round_;
	CVExecutionState *state_;
	std::vector<Socket> sockets_;
};

////////////////////////////////////////////////////////////////////////////////

class NetworkManagerXpilot : public NetworkManager {
 public:

  NetworkManagerXpilot(CVExecutionState* state);

	NetworkManager* clone(CVExecutionState *state);

	void execute_open_socket(CVExecutor* executor,
		klee::KInstruction *target, 
		int domain, int type, int protocol);

	void execute_write(CVExecutor* executor,
		klee::KInstruction *target, 
		klee::ObjectState* object, int fd, int len);

	void execute_read(CVExecutor* executor, 
		klee::KInstruction *target, 
		klee::ObjectState* object, int fd, int len);
};


class NetworkManagerTetrinet : public NetworkManager {
 public:

  NetworkManagerTetrinet(CVExecutionState* state);

	virtual NetworkManager* clone(CVExecutionState *state);

	virtual void execute_read(CVExecutor* executor, 
		klee::KInstruction *target, 
		klee::ObjectState* object, int fd, int len);
};

////////////////////////////////////////////////////////////////////////////////

class NetworkManagerTraining: public NetworkManager {
 public:

  NetworkManagerTraining(CVExecutionState* state);
	virtual NetworkManagerTraining* clone(CVExecutionState *state);

	virtual void add_socket(const KTest* ktest);
	virtual void add_socket(const SocketEventList &log);

	//virtual int socket_log_index(int fd=-1);

	virtual void execute_open_socket(CVExecutor* executor,
		klee::KInstruction *target, 
		int domain, int type, int protocol);

	//virtual void execute_read(CVExecutor* executor, 
	//	klee::KInstruction *target, 
	//	klee::ObjectState* object, int fd, int len);

	//virtual void execute_write(CVExecutor* executor,
	//	klee::KInstruction *target, 
	//	klee::ObjectState* object, int fd, int len);

	virtual void execute_shutdown(CVExecutor* executor,
		klee::KInstruction *target, 
		int fd, int how);
};

////////////////////////////////////////////////////////////////////////////////

//class NetworkManagerTrainingTetrinet: public NetworkManagerTraining {
// public:
//
//  NetworkManagerTrainingTetrinet(CVExecutionState* state);
//	virtual NetworkManagerTraining* clone(CVExecutionState *state);
//
//	//virtual void execute_open_socket(CVExecutor* executor,
//	//	klee::KInstruction *target, 
//	//	int domain, int type, int protocol);
//
//	virtual void execute_read(CVExecutor* executor, 
//		klee::KInstruction *target, 
//		klee::ObjectState* object, int fd, int len);
//
//	//virtual void execute_write(CVExecutor* executor,
//	//	klee::KInstruction *target, 
//	//	klee::ObjectState* object, int fd, int len);
//
//	//virtual void execute_shutdown(CVExecutor* executor,
//	//	klee::KInstruction *target, 
//	//	int fd, int how);
//};

////////////////////////////////////////////////////////////////////////////////


class NetworkManagerFactory {
 public:
  static NetworkManager* create(CVExecutionState* state, ClientVerifier *cv);
};

} // end namespace cliver
#endif // NETWORK_MANAGER_H
