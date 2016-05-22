#include "openssl.h"
#include <assert.h>
#include <netdb.h>
#include <fcntl.h> //Remove on fixing fcntl kludge

#if DEBUG_OPENSSL_MODEL
#define DEBUG_PRINT(x) klee_warning(x);
#else
#define DEBUG_PRINT(x) 
#endif

// override inline assembly version of FD_ZERO from
// /usr/include/x86_64-linux-gnu/bits/select.h
#ifdef FD_ZERO
#undef FD_ZERO
#endif
#define FD_ZERO(p)        memset((char *)(p), 0, sizeof(*(p)))

////////////////////////////////////////////////////////////////////////////////
// KTest socket operations
////////////////////////////////////////////////////////////////////////////////

#if KTEST_SELECT_PLAYBACK

static void print_fd_set(int nfds, fd_set *fds) {
  int i;
  for (i = 0; i < nfds; i++) {
    printf(" %d", FD_ISSET(i, fds));
  }
  printf("\n");
}

DEFINE_MODEL(int, ktest_select, int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, struct timeval *timeout) {
  assert(readfds != NULL);
  assert(exceptfds == NULL);
  static int select_index = -1;

  unsigned int size = 4*nfds + 40 /*text*/ + 3*4 /*3 fd's*/ + 1 /*null*/;
  char *bytes = (char *)calloc(size, sizeof(char));
  int res = cliver_ktest_copy("select", select_index--, bytes, size);
  //klee_warning(bytes);
  //printf("bytes: %s, size: %d, res: %d\n", bytes, size, res);

  // Parse the recorded select input/output.
  char *recorded_select = bytes;
  char *item, *tmp;
  fd_set in_readfds, in_writefds, out_readfds, out_writefds;
  int i, ret, recorded_sockfd, recorded_nfds;

  FD_ZERO(&in_readfds);  // input to select
  if(writefds != NULL) { FD_ZERO(&in_writefds);} // input to select
  FD_ZERO(&out_readfds); // output of select
  if(writefds != NULL) { FD_ZERO(&out_writefds); }// output of select

  tmp = strtok(recorded_select, " ");
  assert(strcmp(tmp, "sockfd") == 0);

  recorded_sockfd = atoi(strtok(NULL, " ")); // socket for TLS traffic

  tmp = strtok(NULL, " ");
  assert(strcmp(tmp, "nfds") == 0);

  recorded_nfds = atoi(strtok(NULL, " "));

  tmp = strtok(NULL, " ");
  assert(strcmp(tmp, "inR") == 0);

  item = strtok(NULL, " ");
  assert(strlen(item) == recorded_nfds);
  for (i = 0; i < recorded_nfds; i++) {
    if (item[i] == '1') {
      FD_SET(i, &in_readfds);
    }
  }

  if(writefds != NULL) {
    tmp = strtok(NULL, " ");
    assert(strcmp(tmp, "inW") == 0);
    item = strtok(NULL, " ");
     assert(strlen(item) == recorded_nfds);
    for (i = 0; i < recorded_nfds; i++) {
      if (item[i] == '1') {
        FD_SET(i, &in_writefds);
      }
    }
  }

  tmp = strtok(NULL, " ");
  assert(strcmp(tmp, "ret") == 0);

  ret = atoi(strtok(NULL, " "));

  tmp = strtok(NULL, " ");
  assert(strcmp(tmp, "outR") == 0);

  item = strtok(NULL, " ");
  assert(strlen(item) == recorded_nfds);
  for (i = 0; i < recorded_nfds; i++) {
    if (item[i] == '1') {
      FD_SET(i, &out_readfds);
    }
  }

  if(writefds != NULL) {
    tmp = strtok(NULL, " ");
    assert(strcmp(tmp, "outW") == 0);

    item = strtok(NULL, " ");
    assert(strlen(item) == recorded_nfds);
    for (i = 0; i < recorded_nfds; i++) {
      if (item[i] == '1') {
        FD_SET(i, &out_writefds);
      }
    }
  }

  free(recorded_select);

  printf("SELECT playback (recorded_nfds = %d, actual_nfds = %d):\n",
          recorded_nfds, nfds);
  printf("  inR: ");
  print_fd_set(recorded_nfds, &in_readfds);
  if(writefds != NULL) printf("  inW: ");
  if(writefds != NULL) print_fd_set(recorded_nfds, &in_writefds);
  printf("  outR:");
  print_fd_set(recorded_nfds, &out_readfds);
  if(writefds != NULL) printf("  outW:");
  if(writefds != NULL) print_fd_set(recorded_nfds, &out_writefds);
  printf("  ret = %d\n", ret);

  // Copy recorded data to the final output fd_sets.
  FD_ZERO(readfds);
  if(writefds != NULL){ FD_ZERO(writefds); }
  int active_fd_count = 0;
  // stdin(0), stdout(1), stderr(2)
  for (i = 0; i < 3; i++) {
    if (FD_ISSET(i, &out_readfds)) {
      FD_SET(i, readfds);
      active_fd_count++;
    }
    if(writefds != NULL) {
      if (FD_ISSET(i, &out_writefds)) {
        FD_SET(i, writefds);
        active_fd_count++;
      }
    }
  }
  // TLS socket (nfds-1)
  if (FD_ISSET(recorded_sockfd, &out_readfds)) {
    //FD_SET(ktest_sockfd, readfds);
    FD_SET(nfds-1, readfds);
    active_fd_count++;
  }
  if(writefds != NULL) {
    if (FD_ISSET(recorded_sockfd, &out_writefds)) {
      //FD_SET(ktest_sockfd, writefds);
      FD_SET(nfds-1, writefds);
      active_fd_count++;
    }
  }
  assert(active_fd_count == ret); // Did we miss anything?

  return ret;
}

DEFINE_MODEL(int, bssl_stdin_ktest_select, int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, struct timeval *timeout){
//I have tested that ktest_select w/ PLAYBACK enabled is working for the stdin
//case, so I am calling it here
  return ktest_select(nfds, readfds, writefds, exceptfds, timeout);
}

#else

DEFINE_MODEL(int, bssl_stdin_ktest_select, int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, struct timeval *timeout) {
  assert(readfds   != NULL);
  assert(exceptfds == NULL);
  assert(writefds  == NULL);

  int i, retval, ret;
  fd_set in_readfds;

  // Copy the read and write fd_sets
  in_readfds = *readfds;

  // Reset for output
  FD_ZERO(readfds);

  int mask_count = nfds/NFDBITS;
  fd_mask all_bits_or = 0;

  for (i = 0; i <= mask_count; ++i) {
    if (in_readfds.fds_bits[i] != 0) {
        fd_mask symbolic_mask;
        klee_make_symbolic(&symbolic_mask, sizeof(fd_mask));
        readfds->fds_bits[i] = in_readfds.fds_bits[i] & symbolic_mask;
        all_bits_or |= readfds->fds_bits[i];
    }
  }

  klee_make_symbolic(&retval, sizeof(retval));

  // Model assumes select does not fail
  if (timeout == NULL) {
    // if timeout is null we assume that at least one bit in the bit
    // is set
    klee_assume(all_bits_or != 0);
    klee_assume(retval > 0);
  } else {
    klee_assume(retval >= 0);
  }

  return retval;
}


DEFINE_MODEL(int, ktest_select, int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, struct timeval *timeout) {
  assert(readfds != NULL);
  assert(exceptfds == NULL);
  //DEBUG_PRINT("symbolic");

  // OPENSSL: Assumption: no sockets ready for reading on the first call to select
  static int first_select = 1;
  assert( !(first_select == 1 && writefds == NULL));

  int i, retval, ret;
  fd_set in_readfds;
  fd_set in_writefds;

  // Copy the read and write fd_sets
  in_readfds = *readfds;
  if(writefds != NULL) {
    in_writefds = *writefds;
  }

  // Reset for output
  FD_ZERO(readfds);
  if(writefds != NULL) {
    FD_ZERO(writefds);
  }

  int mask_count = nfds/NFDBITS;
  fd_mask all_bits_or = 0;

  for (i = 0; i <= mask_count; ++i) {
    if (first_select == 0) {
      if (in_readfds.fds_bits[i] != 0) {
        fd_mask symbolic_mask;
        klee_make_symbolic(&symbolic_mask, sizeof(fd_mask));
        readfds->fds_bits[i] = in_readfds.fds_bits[i] & symbolic_mask;
        all_bits_or |= readfds->fds_bits[i];
      }
    }
  if(writefds != NULL){
    if (in_writefds.fds_bits[i] != 0) {
      fd_mask symbolic_mask;
      klee_make_symbolic(&symbolic_mask, sizeof(fd_mask));
      writefds->fds_bits[i] = in_writefds.fds_bits[i] & symbolic_mask;
      all_bits_or |= writefds->fds_bits[i];
    }
   }
  }

  // Set after first call to select
  if (first_select == 1) {
    first_select = 0;
  }

  klee_make_symbolic(&retval, sizeof(retval));

  // Model assumes select does not fail
  if (timeout == NULL) {
    // if timeout is null we assume that at least one bit in the bit
    // is set
    klee_assume(all_bits_or != 0);
    klee_assume(retval > 0);
  } else {
    klee_assume(retval >= 0);
  }

  return retval;
}

#endif


/*
DEFINE_MODEL(int, ktest_connect, int sockfd, const struct sockaddr *addr, socklen_t addrlen) {
  DEBUG_PRINT("ktest_connect");  
  return 0;
}

DEFINE_MODEL(ssize_t, ktest_writesocket, int fd, const void *buf, size_t count) {
  DEBUG_PRINT("ktest_write_socket");  
  unsigned i;
  char *buf_ptr = buf;
  char *buf_copy = (char*)malloc(count);

  //memcpy(buf_copy, buf, count);
  for (i=0; i<count; ++i)
    buf_copy[i] = buf_ptr[i];

  int result = cliver_socket_write(fd, buf_copy, count, 0);

  free(buf_copy);
  return result;
}

DEFINE_MODEL(ssize_t, ktest_readsocket, int fd, void *buf, size_t count) {
  DEBUG_PRINT("ktest_readsocket");  
  unsigned i;

  char *buf_ptr = buf;
  char *buf_copy = (char*)malloc(count);

  int result = cliver_socket_read(fd, buf_copy, count, 0);
  //memcpy(buf, buf_copy, result);
  for (i=0; i<result; ++i)
    buf_ptr[i] = buf_copy[i];

  free(buf_copy);
  return result;
}
*/


//getaddrinfo is an external call with a malloc.  Therefore accessing any of that
//malloced memory in klee fails.  These are dummy functions that malloc a addrinfo
//and associated sockaddr.  The address is set to 0, which I believe corresponds
//to localhost.
//More work is required to actually make this resolve an address.
//It is also required to overwrite freeaddrinfo
//Returns 0 on success, -1 on failure
int dummy_addrinfo(const char *servname, const struct addrinfo *hints,
  struct addrinfo **res);
/*
++struct addrinfo {
++    int              ai_flags;    //Don't care about -1
++    int              ai_family;   //AF_INET
++    int              ai_socktype; //SOCK_STREAM
++    int              ai_protocol; //SOCKET_PROTOCOL
++    socklen_t        ai_addrlen;
++    struct sockaddr *ai_addr;
++    char            *ai_canonname;//don't care about NULL
++    struct addrinfo *ai_next;     //don't care about NULL
+}
*/
int dummy_addrinfo(const char *port, const struct addrinfo *hints,
  struct addrinfo **res){
    struct sockaddr_in *sa_in;
    struct addrinfo *new_res;

    new_res = (struct addrinfo *)malloc(sizeof(struct addrinfo));
    if(new_res == NULL) return -1;

    new_res->ai_addr = (struct sockaddr *) malloc(sizeof(struct sockaddr_in));
    new_res->ai_addrlen = sizeof(struct sockaddr_in);

    if (new_res->ai_addr == NULL) {
        free(new_res);
        return -1;
    }

   struct in_addr in;
   in.s_addr = 0; //I think localhost is 0

    sa_in = (struct sockaddr_in *)new_res->ai_addr;
    memset(sa_in, 0, sizeof(struct sockaddr_in));
    sa_in->sin_family = PF_INET;
    sa_in->sin_port = atoi(port);
    memcpy(&sa_in->sin_addr, &in, sizeof(struct in_addr));




    new_res->ai_flags     = -1;
    new_res->ai_family    = AF_INET;
    new_res->ai_socktype  = hints->ai_socktype; //SOCK_STREAM;
    new_res->ai_protocol  = hints->ai_protocol; //SOCKET_PROTOCOL;
    new_res->ai_canonname = NULL;
    new_res->ai_next      = NULL;

    *res = new_res;
    return 0;
}



DEFINE_MODEL(int, ktest_getaddrinfo, const char *node, const char *service,
    const struct addrinfo *hints, struct addrinfo **res){
       return dummy_addrinfo(service, hints, res);
}

DEFINE_MODEL(void, ktest_freeaddrinfo, struct addrinfo *res){
    free(res->ai_addr);
    free(res);
}

//THis is a kludge to support bssl.  We need to reconcile the support provided
//in klee's fcntl with the requirements of boringssl.
DEFINE_MODEL(int, ktest_fcntl, int sock, int flags, int not_sure){
    printf("ktest_fcntl MODEL called\n");
    if(flags ==F_GETFL)
        return 1;
    else if(flags == F_SETFL)
        return 0;
    else return -1;
}
