//===-- socket.c -====-----------------------------------------------------===//
//
// <insert license>
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//

#include <time.h> 
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>

static void __emit_error(const char *msg) {
  klee_report_error(__FILE__, __LINE__, msg, "user.err");
}

// NOTE: This network model is INCOMPLETE

// POSIX implementation of htons(3) 
//uint16_t htons(uint16_t hostshort) { 
//unsigned short htons(unsigned short hostshort) { 
//  return hostshort; 
//}

// POSIX implementation of inet_pton(3) 
//int inet_pton(int af, const char *src, void *dst) {
//  return 1;
//}

// POSIX implementation of connect(2)
int connect(int sockfd, const struct sockaddr *serv_addr, socklen_t addrlen) {
  return 0;
}

// POSIX implementation of shutdown(2) 
int shutdown(int sockfd, int how) {
	return cliver_socket_shutdown(sockfd, how);
}

// POSIX implementation of socket(2) 
int socket(int domain, int type, int protocol) {
	return cliver_socket_create(domain, type, protocol);
}

int bind(int sockfd, const struct sockaddr *my_addr, socklen_t addrlen) {
  return 0;
}
ssize_t send(int s, const void *buf, size_t len, int flags) {
  return cliver_socket_write(s, buf, len, flags);
}

ssize_t sendto(int s, const void *buf, size_t len, int flags, 
               const struct sockaddr *to, socklen_t tolen) {
  return cliver_socket_write(s, buf, len, flags);
	//__emit_error("sendto() not supported");
	//return -1;
}

ssize_t recv(int s, void *buf, size_t len, int flags) {
  return cliver_socket_read(s, buf, len, flags);
}

#define SOCKET_REPLAY_ID  1000
#define SOCKET_REPLAY_SERVER_PORT 9999
#define SOCKET_REPLAY_SERVER_NAME "localhost"

ssize_t recvfrom(int s, void *buf, size_t len, int flags,
                 struct sockaddr *from, socklen_t *fromlen) {
  struct sockaddr_in *addr = (struct sockaddr_in*)from;
  addr->sin_family = AF_INET;
  addr->sin_port = SOCKET_REPLAY_SERVER_PORT;
  inet_pton(AF_INET, SOCKET_REPLAY_SERVER_NAME, &addr->sin_addr);
  *fromlen = sizeof(struct sockaddr_in);

	//__emit_error("recvfrom() not supported");
	//return -1;
  return cliver_socket_read(s, buf, len, flags);
}

int getsockname(int s, struct sockaddr *name, socklen_t *namelen) {
  //if (s == SOCKET_REPLAY_ID) {
  //  unsigned short port;
  //  klee_nuklear_make_symbolic(&port, "getsockname_port");
  //  ((struct sockaddr_in*)name)->sin_port = port;
  //  return 0;
  //}
	
  unsigned short port;
  klee_make_symbolic(&port, sizeof(unsigned short), "getsockname_port");
  ((struct sockaddr_in*)name)->sin_port = port;
  return 0;

	//__emit_error("getsockname() not supported");
  //return -1;
}

int setsockopt(int s, int level, int optname, 
               const void *optval, socklen_t optlen) {
  //if (s == SOCKET_REPLAY_ID) {
  //  return 0;
  //}
	//__emit_error("setsockopt() not supported");
  //return -1;
	// XXX SHOULD BE IMPLEMENTED IN CLIVER: cliver_setsockopt()
	return 0;
}

int gettimeofday(struct timeval *tv, struct timezone *tz) {
  if (tv == NULL) 
    return -1; 
  klee_warning_once("using symbolic time model in gettimeofday");
  klee_make_symbolic(tv, sizeof(struct timeval), "gettimeofday");
  return 0;
}

time_t __time(time_t *t) {
  time_t __t;
  klee_make_symbolic(&__t, sizeof(time_t), "__time");

  if (t) 
		t = &__t;

  return __t;
}

int cliver_training_start() {
  return 0;
}

