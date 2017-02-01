// RUN: echo 0

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <time.h>

#ifdef KLEE
#include "klee/klee.h"
unsigned klee_is_symbolic_buffer(const unsigned char *buf, size_t len);
#endif

#ifdef KTEST
char* ktest_file = KTEST;
#else
char* ktest_file = "lazyconstraint.ktest";
#endif

#include "klee/Internal/ADT/KTest.h"
#include "KTestSocket.inc"

enum bitcode_test_case {NETWORK_DATA, IMPLIED_VALUE_CONCRETIZATION};
enum mode_type {CLIENT_MODE, SERVER_MODE, FORK_MODE};
enum lazy_constraint_type {LAZY_NONE, LAZY_P, LAZY_P_INV, LAZY_P_BOTH};

#define BUFFER_SIZE 256
static int NEGATIVE_TEST_CASE = 0;  // intentionally send wrong message
static int FORCE_X_EQUAL_TEN = 0;   // for IVC concrete run, force x = 10
static int ENABLE_PROHIBITIVE = 0;  // mark prohibitive_f as prohibitive
static int ENABLE_LAZY = LAZY_NONE; // enable lazy constraint generators

/**************************** TEST DESCRIPTION ****************************

Let p and p_inv be the following "prohibitive" function and its inverse:

  unsigned int p(unsigned int x) {
    return 641 * x;
  }

  unsigned int p_inv(unsigned int x) {
    return 6700417 * x;
  }

Note that this is based on the fact that the Fermat number F_5 = 2^32
+ 1 has factorization 641 * 6700417. We construct a simple program
(pseudocode) for testing the operation of lazy constraint generation.

  int main()
  {
    unsigned int x, y;
    MAKE_SYMBOLIC(&x);
    y = p(x);
    SEND(y);
    SEND(x);
    return 0;
  }

  // Positive test case: 6410, 10
  // Negative test case: 6410, 11

We should test this program with both the forward lazy constraint generator (p)
or the inverse lazy constraint generator (p_inv).  In the forward case, this
tests whether constraints can propagate across rounds.

A slightly trickier test case is this one, where the concretization of a
variable is implied by the path constraint rather than by explicit assignment
at a network send point:

  int main()
  {
    unsigned int x, y;
    MAKE_SYMBOLIC(&x);
    y = p(x);
    SEND(314);
    if (x == 10) {
      SEND(y);
    } else {
      SEND(159);
    }
    return 0;
  }

  // Positive test case: 314, 159
  // Positive test case: 314, 6410
  // Negative test case: 314, 6411

Below, we use the more descriptive names prohibitive_f() and
prohibitive_f_inverse() as wrappers for the functions p() and p_inv() above.
**************************************************************************/

////////////////////////////////////////////////////////////////////////////////
// KLEE related tooling
////////////////////////////////////////////////////////////////////////////////

#ifdef KLEE

// Allocate a temporary symbolic buffer and copy into buf; this is needed
// because klee_make_symbolic looks up the allocated object for a buf pointer
// and will fail on non-aligned pointers.
void copy_symbolic_buffer(unsigned char *buf, int len, char *tag) {
  if (buf) {
    unsigned char *symbolic_buf = (unsigned char *)malloc(len);
    klee_make_symbolic(symbolic_buf, len, tag);
    memcpy(buf, symbolic_buf, len);
    free(symbolic_buf);
  }
}

#endif // KLEE

////////////////////////////////////////////////////////////////////////////////

// "Complicated" function and its inverse.

unsigned int p(unsigned int x) {
  return 641 * x;
}

unsigned int p_inv(unsigned int x) {
  return 6700417 * x;
}

// "Prohibitive" function version, which will skip p() on symbolic input,
// and possibly add lazy constraints.
unsigned int prohibitive_f(unsigned int x) {
  if (ENABLE_PROHIBITIVE) {
#ifdef KLEE
    // If symbolic input, skip.
    if(klee_is_symbolic_buffer((unsigned char *)&x, sizeof(x))) {
      unsigned int ret;
      copy_symbolic_buffer((unsigned char *)&ret, (int)sizeof(ret), "pOut");
      // TODO: Inject lazy constraint(s)
      printf("skipping p()\n");
      return ret;
    }
#else
    fprintf(stderr, "prohibitive functions require compiling with -DKLEE\n");
    exit(1);
#endif // KLEE
  }
  printf("running concrete p()\n");
  return p(x);
}



//static unsigned long int srand_next = 1;
void srand(unsigned int seed) {
#ifdef KLEE
  //srand_next = seed;
#else
  srandom(seed);
#endif
}

int rand() {
#ifdef KLEE
  //srand_next = srand_next * 1103515245 + 12345;
  //return (unsigned int)(srand_next / 65536) % 32768;
  return klee_int("rand");
#else
  return random();
#endif
}


// Initialize simple server
void server_init(int *port, int *fd) {
  struct sockaddr_in server_address;

  if ((*fd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    goto exit_error;

  memset(&server_address, '0', sizeof(server_address));
  server_address.sin_family = AF_INET;
  server_address.sin_addr.s_addr = INADDR_ANY;
  server_address.sin_port = htons(*port);

  if (bind(*fd, (struct sockaddr*)&server_address, sizeof(server_address)) < 0)
    goto exit_error;

  if (listen(*fd, 1) != 0)
    goto exit_error;

  socklen_t len = sizeof(server_address);
  if (getsockname(*fd, (struct sockaddr *)&server_address, &len) == -1)
    goto exit_error;

  *port = ntohs(server_address.sin_port);

  printf("SERVER: binding on port %d\n", *port);

  return;

exit_error:
  perror("server_init error");
  exit(1);
}

// Run simple server: print c2s messages to screen (and record c2s ktest)
void server_run(int listen_fd) {
  int client_fd;
  char recv_buffer[BUFFER_SIZE];
  unsigned int recv_int;
  int NUM_INTS_TO_RECV = 2;

  if ((client_fd = accept(listen_fd, NULL, NULL)) < 0)
    goto exit_error;

  // Receive some integers
  for (int i = 0; i < NUM_INTS_TO_RECV; i++) {
    if (ktest_recv(client_fd, &recv_int, sizeof(recv_int), 0) !=
        sizeof(recv_int))
      goto exit_error;
    printf("SERVER: recv: (uint32) %u\n", recv_int);
  }

  // Receive "QUITTING" message
  memset(recv_buffer, 0, sizeof(recv_buffer));
  if (ktest_recv(client_fd, recv_buffer, sizeof(recv_buffer), 0) == -1)
    goto exit_error;
  printf("SERVER: recv: %s\n", recv_buffer);

  close(client_fd);
  close(listen_fd);

  printf("SERVER: success!\n");
  return;

exit_error:
  perror("server_run error");
  exit(1);

}

// Initialize simple client
void client_init(int port, int *client_fd) {
  char* ip_address = "127.0.0.1";
  struct sockaddr_in server_address;

  printf("CLIENT: connecting to localhost:%d\n", port);

  srand(time(NULL));

  if ((*client_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    goto exit_error;

  server_address.sin_family = AF_INET;
  server_address.sin_port = htons(port);

  if (inet_pton(AF_INET, ip_address, &server_address.sin_addr) <= 0)
    goto exit_error;

  if (connect(*client_fd, (const struct sockaddr *)&server_address,
              sizeof(server_address)) < 0)
    goto exit_error;

  return;

exit_error:
  perror("client_init error");
  exit(1);
}

void client_run_net(int client_fd) {
  char send_buffer[BUFFER_SIZE];
  unsigned int x, y;

  memset(send_buffer, '\0', sizeof(send_buffer));
  x = (unsigned int)rand();
  y = prohibitive_f(x);

  // Send y = p(x)
#ifdef KLEE
  printf("CLIENT: send y (uint32) = ??\n");
#else
  printf("CLIENT: send y (uint32) = %u\n", y);
#endif
  if (send(client_fd, &y, sizeof(y), 0) < 0)
    goto exit_error;

  // Send x
  if (NEGATIVE_TEST_CASE) {
    x += 1;
  }
#ifdef KLEE
  printf("CLIENT: send x (uint32) = ??\n");
#else
  printf("CLIENT: send x (uint32) = %u\n", x);
#endif
  if (send(client_fd, &x, sizeof(x), 0) < 0)
    goto exit_error;

  // Send quit
  sprintf(send_buffer, "QUITTING");
  printf("CLIENT: send: %s\n", send_buffer);
  if (send(client_fd, send_buffer, strnlen(send_buffer,BUFFER_SIZE)+1, 0) < 0)
    goto exit_error;

  close(client_fd);
  printf("CLIENT: success!\n");

  return;

exit_error:
  perror("client_run error");
  exit(1);
}

void client_run_ivc(int client_fd) {
  char send_buffer[BUFFER_SIZE];
  unsigned int x, y;

  memset(send_buffer, '\0', sizeof(send_buffer));
  x = (unsigned int)rand();
  if (FORCE_X_EQUAL_TEN) {
    x = 10;
  }
  y = prohibitive_f(x);

  // Send 314
  unsigned int z = 314;
  printf("CLIENT: send 314 (uint32)\n");
  if (send(client_fd, &z, sizeof(z), 0) < 0)
    goto exit_error;

  // Send next one depending on what x was.
  if (x == 10) {
    if (NEGATIVE_TEST_CASE) {
      y += 1;
    }
#ifdef KLEE
    printf("CLIENT: send y (uint32) = ??\n");
#else
    printf("CLIENT: send y (uint32) = %u\n", y);
#endif
    if (send(client_fd, &y, sizeof(y), 0) < 0)
      goto exit_error;
  } else {
    unsigned int w = 159;
    printf("CLIENT: send 159 (uint32)\n");
    if (send(client_fd, &w, sizeof(w), 0) < 0)
      goto exit_error;
  }

  // Send quit
  sprintf(send_buffer, "QUITTING");
  printf("CLIENT: send: %s\n", send_buffer);
  if (send(client_fd, send_buffer, strnlen(send_buffer,BUFFER_SIZE)+1, 0) < 0)
    goto exit_error;

  close(client_fd);
  printf("CLIENT: success!\n");

  return;

exit_error:
  perror("client_run error");
  exit(1);
}

void client_run(int client_fd, int lcg_test_type) {
  if (lcg_test_type == NETWORK_DATA) {
    client_run_net(client_fd);
  }
  else if (lcg_test_type == IMPLIED_VALUE_CONCRETIZATION) {
    client_run_ivc(client_fd);
  }
}

int main(int argc, char* argv[]) {
  int listen_fd=-1, client_fd=-1, port=0;
  int c;

#if defined (CLIENT)
  int mode=CLIENT_MODE;
#else
  int mode=FORK_MODE;
#endif

  int lcg_test_type = NETWORK_DATA; // default test type

  while ((c = getopt(argc, argv, "csfp:m:b:k:nitPL:")) != -1) {
    switch (c) {
      case 'c':
        mode=CLIENT_MODE;
        break;
      case 's':
        mode=SERVER_MODE;
        break;
      case 'f':
        mode=FORK_MODE;
        break;
      case 'k':
        ktest_file=(char*)malloc(strlen(optarg)+1);
        memcpy(ktest_file,(char*)optarg,strlen(optarg)+1);
        break;
      case 'p':
        port = (int)atoi(optarg);
        break;
      case 'n':
        NEGATIVE_TEST_CASE = 1;
        break;
      case 'i':
        lcg_test_type = IMPLIED_VALUE_CONCRETIZATION;
        break;
      case 't':
        FORCE_X_EQUAL_TEN = 1;
        break;
      case 'P':
        ENABLE_PROHIBITIVE = 1;
        break;
      case 'L':
        if (strcmp(optarg, "p") == 0) {
          ENABLE_LAZY = LAZY_P;
        } else if (strcmp(optarg, "p_inv") == 0) {
          ENABLE_LAZY = LAZY_P_INV;
        } else if (strcmp(optarg, "p_both") == 0) {
          ENABLE_LAZY = LAZY_P_BOTH;
        } else {
          fprintf(stderr, "invalid argument to -L\n");
          exit(1);
        }
        break;
    }
  }

  if (mode == CLIENT_MODE) {
    client_init(port, &client_fd);
    client_run(client_fd, lcg_test_type);
  } else if (mode == SERVER_MODE) {
    server_init(&port, &listen_fd);
    server_run(listen_fd);
    ktest_finish();
  } else if (mode == FORK_MODE) {
    server_init(&port, &listen_fd);
    int pid = fork();
    if (pid == 0) {
      server_run(listen_fd);
      ktest_finish();
    } else {
      client_init(port, &client_fd);
      client_run(client_fd, lcg_test_type);
    }
  }
  return 0;
}
