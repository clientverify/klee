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
}

namespace cliver {

class CVExecutor;
class CVExecutionState;
class ClientVerifier;

class NetworkManager {
 public:

  NetworkManager(CVExecutionState* state);

	virtual void add_socket(const KTest* ktest);
	virtual void add_socket(const SocketEventList &log);
	virtual void add_socket(const std::string &ktest_text_file,
	                        bool drop_s2c_tls_appdata);
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
	std::vector<Socket>& sockets() { return sockets_; }
	Socket* socket(int fd=-1);

 protected:
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

////////////////////////////////////////////////////////////////////////////////

class NetworkManagerFactory {
 public:
  static NetworkManager* create(CVExecutionState* state, ClientVerifier *cv);
};

} // end namespace cliver
#endif // NETWORK_MANAGER_H
