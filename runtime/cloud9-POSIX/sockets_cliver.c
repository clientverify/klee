//===-- socket.c -====-----------------------------------------------------===//
//
// <insert license>
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <unistd.h>
#include <assert.h>

#include <time.h> 
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include "../../include/klee/klee.h"

// override inline assembly version of FD_ZERO from
// /usr/include/x86_64-linux-gnu/bits/select.h
#ifdef FD_ZERO
#undef FD_ZERO
#endif
#define FD_ZERO(p)        memset((char *)(p), 0, sizeof(*(p)))

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
  klee_warning("called connect()");
  return 0;
}

// POSIX implementation of shutdown(2) 
int shutdown(int sockfd, int how) {
  klee_warning("called shutdown()");
  return cliver_socket_shutdown(sockfd, how);
}

// POSIX implementation of socket(2) 
int socket(int domain, int type, int protocol) {
  klee_warning("called socket()");
  return cliver_socket_create(domain, type, protocol);
}

int bind(int sockfd, const struct sockaddr *my_addr, socklen_t addrlen) {
  klee_warning("called bind()");
  return 0;
}

ssize_t send(int s, const void *buf, size_t len, int flags) {
  klee_warning("called send()");
  return cliver_socket_write(s, buf, len, flags);
}

ssize_t sendto(int s, const void *buf, size_t len, int flags, 
               const struct sockaddr *to, socklen_t tolen) {
  klee_warning("called sendto()");
  return cliver_socket_write(s, buf, len, flags);
}

ssize_t recv(int s, void *buf, size_t len, int flags) {
  klee_warning("called recv()");
  return cliver_socket_read(s, buf, len, flags);
}

#define SOCKET_REPLAY_ID  1000
#define SOCKET_REPLAY_SERVER_PORT 9999
#define SOCKET_REPLAY_SERVER_NAME "localhost"

ssize_t recvfrom(int s, void *buf, size_t len, int flags,
                 struct sockaddr *from, socklen_t *fromlen) {
  klee_warning("called recvfrom()");
  struct sockaddr_in *addr = (struct sockaddr_in*)from;
  addr->sin_family = AF_INET;
  addr->sin_port = SOCKET_REPLAY_SERVER_PORT;
  inet_pton(AF_INET, SOCKET_REPLAY_SERVER_NAME, &addr->sin_addr);
  *fromlen = sizeof(struct sockaddr_in);

  return cliver_socket_read(s, buf, len, flags);
}

int getsockname(int s, struct sockaddr *name, socklen_t *namelen) {
  klee_warning("called getsockname()");

  unsigned short port;
  klee_make_symbolic(&port, sizeof(unsigned short), "getsockname_port");
  ((struct sockaddr_in*)name)->sin_port = port;
  return 0;
}
#include "sockets.h"
int getsockopt (int sockfd, int level, int optname, void *optval, socklen_t *optlen) {

  klee_warning("called getsockopt()");

  socket_t *sock = (socket_t*) __fdt[sockfd].io_object;

  switch (level) {
  case SOL_SOCKET:
    switch (optname) {
    case SO_ACCEPTCONN:
      if (*optlen < sizeof(int)) {
        return -1;
      }

      *((int*) optval) = (sock->status == SOCK_STATUS_LISTENING);
      *optlen = sizeof(int);
      break;
    case SO_TYPE:
      *((int*)optval) = 1;
      return 0;
      break;
    case SO_ERROR:
      if (*optlen < sizeof(int)) {
        return -1;
      }
      // XXX: We currently do not support any of the possible socket errors
      *((int*) optval) = 0;
      *optlen = sizeof(int);
      break;
    default:
      klee_warning("unsupported optname");
      return -1;
    }
    break;
  default:
    klee_warning("unsupported level");
    return -1;
  }
  return 0;
}

int setsockopt(int s, int level, int optname, 
               const void *optval, socklen_t optlen) {
  klee_warning("called setsockopt()");
  return 0;
}

/*
int gettimeofday(struct timeval *tv, struct timezone *tz) {
  klee_warning("called gettimeofday()");
  if (tv == NULL) 
    return -1; 
  klee_warning_once("using symbolic time model in gettimeofday");
  klee_make_symbolic(tv, sizeof(struct timeval), "gettimeofday");
  return 0;
}
*/

time_t __time(time_t *t) {
  klee_warning("called __time()");
  time_t __t;
  klee_make_symbolic(&__t, sizeof(time_t), "__time");

  if (t) 
		t = &__t;

  return __t;
}

int cliver_training_start() {
  return 0;
}

// Network capture for Cliver
int __klee_model_ktest_connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen) {
  klee_warning("ktest_connect()");
  return 0;
}

ssize_t __klee_model_ktest_writesocket(int fd, const void *buf, size_t count) {
  unsigned i;

  // HACK NetworkManager doesn't support non-ObjectState aligned pointers
  //return cliver_socket_write(fd, buf, count, 0);
  
  char *buf_ptr = buf;
  char *buf_copy = (char*)malloc(count);

  for (i=0; i<count; ++i) {
    buf_copy[i] = buf_ptr[i];
  }
  int res = cliver_socket_write(fd, buf_copy, count, 0);
  free(buf_copy);
  return res;
}

ssize_t __klee_model_ktest_readsocket(int fd, void *buf, size_t count) {
  unsigned i;

  // HACK NetworkManager doesn't support non-ObjectState aligned pointers
  //return cliver_socket_read(fd, buf, count, 0);
  
  char *buf_ptr = buf;
  char *buf_copy = (char*)malloc(count);

  int res = cliver_socket_read(fd, buf_copy, count, 0);
  for (i=0; i<res; ++i) {
    buf_ptr[i] = buf_copy[i];
  }
  free(buf_copy);
  return res;
}
