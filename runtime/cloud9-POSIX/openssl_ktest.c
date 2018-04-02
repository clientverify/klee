#include "openssl.h"
#include <assert.h>
#include <netdb.h>
#include <fcntl.h> //Remove on fixing fcntl kludge
#include <errno.h>

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


monitor_socket = -1;
net_socket = -1;
#define MAX_FDS 32  //total number of socket file descriptors we will support
static int ktest_nfds = 0; //total number of socket file descriptors in system
static int ktest_sockfds[MAX_FDS]; // descriptor of the sockets we're capturing
                                  //values initialized to -1 in ktest_start.
static int ktest_bind_sockfd = -1;



//This overaproximates the filedescriptors in the system.  We don't support
//a close model at the moment--which is probably going to bite us at some point.
void klee_insert_ktest_sockfd(int sockfd){
  static int initial_call = 1;
  if(initial_call){
    printf("klee_insert_ktest_sockfd initial call\n");
    int i;
    for(i = 0; i < MAX_FDS; i++){
      assert(ktest_sockfds[i] == 0);
      ktest_sockfds[i] = -1;
    }
    initial_call = 0;
  }
  int i;
  for(i = 0; i < ktest_nfds; i++){
    if(ktest_sockfds[i] == sockfd){
      printf("klee_insert_ktest_sockfd attempting to add duplicate sockfd %d\n", sockfd);
      return;
    }
  }
  printf("klee_insert_ktest_sockfd adding %d to ktest_sockfds ktest_nfds %d\n", sockfd, ktest_nfds);
  assert(ktest_nfds + 1 < MAX_FDS);
  ktest_sockfds[ktest_nfds] = sockfd; // record the socket descriptor of interest
  ktest_nfds++; //incriment the counter recording the number of sockets we're tracking
}

DEFINE_MODEL(int, ktest_close, int fd){
  printf("klee's ktest_close doing nothing for fd %d\n", fd);
  return 0;
}

DEFINE_MODEL(int, ktest_monitor_socket, int domain, int type, int protocol){
  assert(monitor_socket == -1); //should only be called once
  monitor_socket = socket(domain, type, protocol);
  printf("ktest_socket adding monitor_socket %d\n", monitor_socket);
  klee_insert_ktest_sockfd(monitor_socket);
  return monitor_socket;
}

DEFINE_MODEL(int, ktest_socket, int domain, int type, int protocol){
  static int hack_fd = -1;
  if(hack_fd == -1) hack_fd = monitor_socket;
  hack_fd++;
  printf("ktest_socket adding sockfd %d\n", hack_fd);
  klee_insert_ktest_sockfd(hack_fd);
  return hack_fd;
}

DEFINE_MODEL(int, ktest_dup, int oldfd){
  int newfd = ktest_socket(AF_INET, SOCK_STREAM, 0);
  printf("klee's dup returning %d\n", newfd);
  return newfd;
}


DEFINE_MODEL(int, ktest_socketpair, int domain, int type, int protocol, int sv[2]){
  sv[0] = ktest_socket(domain, type, protocol);
  sv[1] = ktest_socket(domain, type, protocol);
  printf("ktest_socketpair adding sockfds %d %d\n", sv[0], sv[1]);
  return 0;
}


DEFINE_MODEL(int, ktest_pipe, int pipefd[2]){
  pipefd[0] = ktest_socket(AF_INET, SOCK_STREAM, 0);
  pipefd[1] = ktest_socket(AF_INET, SOCK_STREAM, 0);
  printf("ktest_pipe adding sockfds %d %d\n", pipefd[0], pipefd[1]);
  return 0;
}

//Need to go back and change this:
DEFINE_MODEL(size_t, strlcat, char *dst, const char *src, size_t size){
  return strcat(dst, src);
}

//Need to go back and change this:
DEFINE_MODEL(size_t, strlcpy, char *dst, const char *src, size_t size){
  return strcpy(dst, src);
}

//We assume that there is only one tty at any point.
static int only_ttyfd = -1;
DEFINE_MODEL(char*, ktest_ttyname, int fd){
  //read from ktest here...
  static int ttyname_index = -1;
  unsigned int size = 100;//max len
  char *bytes = (char *)calloc(size, sizeof(char));
  int res = cliver_ktest_copy("ttyname", ttyname_index--, bytes, size);

  assert(res <= size);
  char *ret = strdup((const char*)bytes);
  free(bytes);
  return ret;
}

DEFINE_MODEL(int, ktest_openpty, int *ptyfd, int *ttyfd, char *name, const struct termios *termp, const struct winsize *winp){
  *ptyfd = ktest_socket(AF_INET, SOCK_STREAM, 0);
  *ttyfd = ktest_socket(AF_INET, SOCK_STREAM, 0);

  only_ttyfd = *ttyfd;
  printf("klee's ktest_openpty adding sockfds ptyfd %d ttyfd %d\n", *ptyfd, *ttyfd);
  return 0;
}

//What we want is for the st_uid and st_gid to match the pw->uid and pw->gid
//in OpenSSH's pty_setowner.
DEFINE_MODEL(int, ktest_stat, const char *path, struct stat *buf){
  buf->st_uid = getuid();  //assume these are the ones assigned to the file?
  buf->st_gid = getgid();
  buf->st_mode = 8592;//this is what it is in the replay without klee
  printf("klee's ktest_stat returning: st_uid %d, st_gid %d, st_mode %d\n",
      buf->st_uid,  buf->st_gid, buf->st_mode);
  return 0; //assume success.
}

DEFINE_MODEL(int, ktest_chown, const char *pathname, uid_t owner, gid_t group){
  return 0;
}

DEFINE_MODEL( int, ktest_initgroups, const char *user, gid_t group){
  printf("klee's ktest_initgroups called with user %s group %d\n",
      user, group);
  return 0;
}

DEFINE_MODEL( int, ktest_setgroups, size_t size, const gid_t *list){
  printf("klee's ktest_setgroups called with size %d\n", size);
  return 0;
}

DEFINE_MODEL(int, ktest_bind, int sockfd, const struct sockaddr *addr, socklen_t addrlen) {
  if( ktest_bind_sockfd != -1) //if ktest_bind_sockfd is already assigned, return error
    return -1;
  ktest_bind_sockfd = sockfd; // record the socket descriptor of interest
  printf("klee's bind() called on socket %d\n", sockfd);
  return 0; // assume success
}

DEFINE_MODEL(int, ktest_accept, int sockfd, struct sockaddr *addr, socklen_t *addrlen){
  printf("entered klee's ktest_accept adding calling socket\n");
  int accept_sock = ktest_socket(AF_INET, SOCK_STREAM, 0);
  printf("ktest_accept adding %d to ktest_sockfds\n", accept_sock);
  assert(accept_sock >= 0);
  printf("accept() called on socket for TLS traffic (%d)\n", accept_sock);
  if(accept_sock > -1){
    printf("klee's ktest_accept setting net_socket %d from %d\n", accept_sock, net_socket);
    assert(net_socket == -1);
    net_socket = accept_sock;
  }

  return accept_sock;

}


//Return the same fake pid everytime for debugging.
#define KTEST_FORK_DUMMY_CHILD_PID 37

//which is the parent or child--whichever we wish to continue
//recording or playing back from
DEFINE_MODEL(pid_t, ktest_fork, enum KTEST_FORK which){
  if(which == PARENT){ //we recorded the parent
    //return a positive non-0 value.
    //Note: we assume there is no communication between
    //parent and child in the recorded case.  If there is,
    //then we're in trouble.
    //Must correspond with the pid eventually returned from
    //ktest_waitpid.
    return KTEST_FORK_DUMMY_CHILD_PID;
  } else { //we recorded the child, return current pid.
    //not guarenteed to be the same as when recorded.
    return 0;
  }
}

///Pam functions:
DEFINE_MODEL(int, pam_start, const char *service_name, const char *user, const struct pam_conv *pam_conversation, pam_handle_t **pamh){
  printf("klee's pam_start model\n");
  return PAM_SUCCESS;
}

DEFINE_MODEL(int, pam_set_item, pam_handle_t *pamh, int item_type, const void *item){
  printf("klee's pam_set_item model\n");
  return PAM_SUCCESS;
}

DEFINE_MODEL(int, pam_end, pam_handle_t *pamh, int pam_status){
  printf("klee's pam_end model\n");
  return PAM_SUCCESS;
}

DEFINE_MODEL(int, pam_acct_mgmt, pam_handle_t *pamh, int flags){
  printf("klee's pam_acct_mgmt model\n");
  return PAM_SUCCESS;
}

DEFINE_MODEL(int, pam_setcred, pam_handle_t *pamh, int flags){
  printf("klee's pam_setcred model\n");
  return PAM_SUCCESS;
}

DEFINE_MODEL(int, pam_authenticate, pam_handle_t *pamh, int flags){
  printf("klee's pam_authenticate model\n");
  return PAM_SUCCESS;
}

DEFINE_MODEL(int, pam_open_session, pam_handle_t *pamh, int flags){
  printf("klee's pam_open_session model\n");
  return PAM_SUCCESS;
}

DEFINE_MODEL(int, ktest_shutdown, int socket, int how){
  printf("klee's ktest_shutdown\n");
  return 0;
}

//Used in openssh, all that sshd cares about is the p_proto field
DEFINE_MODEL(struct protoent*, getprotobyname, const char *name){
  assert(strcmp("ip", name) == 0);
  static struct protoent proto;
  proto.p_proto   = IPPROTO_IP;
  proto.p_aliases = NULL;
  proto.p_name    = NULL;
  return &proto;
}


DEFINE_MODEL(ssize_t, ktest_recvmsg_fd, int fd, struct msghdr *msg, int flags){
  int sockfd = ktest_socket(AF_INET, SOCK_STREAM, 0);
  assert(sockfd >= 0); //Ensure fd creation was successful.

  //The following is highly specific to what is checked inmm_recieve_fd in
  //ssh codebase.
  struct cmsghdr *cmsg;
  cmsg = CMSG_FIRSTHDR(msg);
  cmsg->cmsg_type = SCM_RIGHTS;
  memcpy(CMSG_DATA(cmsg), &sockfd, sizeof(sockfd));

  int expected_return = 1;
  return expected_return;
}

static int (*signal_handler)(int);
DEFINE_MODEL(int, ktest_register_signal_handler, int (*a)(int)){
  printf("klee's ktest_register_signal_handler called\n");
  signal_handler = a;
}

static int waitpid_had_ECHILD = 0;
DEFINE_MODEL(int, ktest_waitpid_or_error, pid_t pid, int *status, int options){
  static int waitpid_index = -1;
  printf("klee's ktest_waitpid_or_error entered\n");

  unsigned int size = 100;
  char *bytes = (char *)malloc(size);
  int res = cliver_ktest_copy("waitpid", waitpid_index--, bytes, size);
  char *recorded_select = bytes;
  char* tmp = strtok(recorded_select, " ");
  assert(strcmp(tmp, "ret_val") == 0);
  int ret_val = atoi(strtok(NULL, " "));

  if(ret_val >= 0){
    tmp = strtok(NULL, " ");
    //remove print after tested
    printf("ktest_waitpid status == %s\n", tmp);
    assert(strcmp(tmp, "status") == 0);
    *status = atoi(strtok(NULL, " "));
    printf("ktest_waitpid status %d\n", *status);
  } else {
    tmp = strtok(NULL, " ");
    //remove print after tested
    printf("ktest_waitpid errno == %s\n", tmp);
    assert(strcmp(tmp, "errno") == 0);
    errno = atoi(strtok(NULL, " "));
    assert(errno == ECHILD);
    waitpid_had_ECHILD = 1;
    printf("ktest_waitpid error %d\n", errno);
  }
  free(recorded_select);
  if(ret_val == 0){
    printf("ktest_waitpid returning 0\n");
    return 0;
  } else if(ret_val <  0){
    return -1;
  } else {
    printf("ktest_waitpid returning pid %d status %d\n",
        KTEST_FORK_DUMMY_CHILD_PID, *status);
    return KTEST_FORK_DUMMY_CHILD_PID;
  }
}

//does not support verification of this socket.
DEFINE_MODEL(int, ktest_readsocket_or_error, int fd, void *buf, size_t count){
  assert(!klee_is_symbolic(count));
  assert(!klee_is_symbolic(buf)); //the pointer shouldn't be symbolic.
  assert(!klee_is_symbolic(fd));

  assert(monitor_socket != fd);
  assert(net_socket != fd);

  printf("klee's ktest_readsocket_or_error entered\n");
  if(waitpid_had_ECHILD == 1){
    errno = EIO;
    return -1;
  } else {
    return ktest_readsocket(fd, buf, count);
  }
}


//does not support verification of this socket.
DEFINE_MODEL(int, ktest_writesocket, int fd, void *buf, size_t count){
  assert(!klee_is_symbolic(count));
  assert(!klee_is_symbolic(buf)); //the pointer shouldn't be symbolic.
  assert(!klee_is_symbolic(fd));
  printf("klee's ktest_writesocket entered\n");

  if (monitor_socket == fd || net_socket == fd)
    return ktest_verify_writesocket(fd, buf, count);

#if KTEST_WRITESOCKET_PLAYBACK
  static int writesocket_index = -1;
  char *bytes = (char *)calloc(count, sizeof(char));
  int res = cliver_ktest_copy("writesocket", writesocket_index--, bytes, count);


  if (res > count) {
    fprintf(stderr,
        "ktest_writesocket playback error: %zu bytes of input, "
        "%d bytes recorded\n", count, res);
    exit(2);
  }

  int i;
  printf("writesocket record contains write [%d]", res);
  for (i = 0; i < res; i++) {
    printf(" %2.2x", ((unsigned char*)bytes)[i]);
  }
  printf("\n");


  // Since this is a write, compare for equality.
  if (res > 0 && memcmp(buf, bytes, res) != 0) {
    fprintf(stderr, "WARNING: ktest_writesocket playback - data mismatch\n");
    //trying to send:
    unsigned int i;
    printf("writesocket trying to write [%d]", count);
    for (i = 0; i < res; i++) {
      printf(" %2.2x", ((unsigned char*)bytes)[i]);
    }
    printf("\n");
    //and fail
    assert(0);
  }
  return res;
#else
  return count;
#endif
}

//does not support verification of this socket.
DEFINE_MODEL(int, ktest_readsocket, int fd, void *buf, size_t count){
  assert(!klee_is_symbolic(count));
  assert(!klee_is_symbolic(buf)); //the pointer shouldn't be symbolic.
  assert(!klee_is_symbolic(fd));
  printf("klee's ktest_readsocket entered\n");

  if (monitor_socket == fd || net_socket == fd)
    return ktest_verify_readsocket(fd, buf, count);

#if KTEST_READSOCKET_PLAYBACK
  static int readsocket_index = -1;
  char *bytes = (char *)calloc(count, sizeof(char));
  int res = cliver_ktest_copy("readsocket", readsocket_index--, bytes, count);
  if (res > count) {
    fprintf(stderr,
        "ktest_readsocket playback error: %zu byte destination buffer, "
        "%d bytes recorded", count, res);
    exit(2);
  }
  // Read recorded data into buffer
  memcpy(buf, bytes, res);

  //error stuff:  
  unsigned int i;
  printf("readsocket playback [%d]", res);
  for (i = 0; i < res; i++) {
    printf(" %2.2x", ((unsigned char*)buf)[i]);
  }
  printf("\n");
  //end error stuff
  return res;

#else

  char* buf_name = "ktest_readsocket";
  klee_make_symbolic(buf, count, buf_name);

  char* len_name = "ktest_readsocket_len";
  size_t ret_len;
  klee_make_symbolic(&ret_len, sizeof(ret_len), len_name);
  klee_assume(ret_len <= count);
  klee_assume(ret_len >= 0);
  return ret_len;

#endif
}


DEFINE_MODEL(int, ktest_getpeername, int sockfd, struct sockaddr *addr, socklen_t *addrlen){
  static int get_peer_name_index = -1;
  unsigned int size = *addrlen;
  char *bytes = (char *)calloc(size, sizeof(char));
  int res = cliver_ktest_copy("get_peer_name", get_peer_name_index--, bytes, size);

  *addrlen = res;
  memcpy(addr, bytes, res);

  unsigned int i;
  printf("getpeername playback [%d]", res);
  for (i = 0; i < res; i++) {
    printf(" %2.2x", ((unsigned char*)addr)[i]);
  }
  printf("\n");

  return 0; //assume success

}

static void print_fd_set(int nfds, fd_set *fds) {
  int i;
  for (i = 0; i < nfds; i++) {
    printf(" %d", FD_ISSET(i, fds));
  }
  printf("\n");
}


#if KTEST_SELECT_PLAYBACK
DEFINE_MODEL(int, ktest_select_and_signal, int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, struct timeval *timeout) {
  return ktest_select(nfds, readfds, writefds, exceptfds, timeout);
}

DEFINE_MODEL(int, ktest_select, int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, struct timeval *timeout) {
  printf("klee's select entered\n");
  assert(readfds != NULL);
  assert(exceptfds == NULL);
  static int select_index = -1;

  unsigned int size = 4*nfds + 40 /*text*/ + 3*4 /*3 fd's*/ + 1 /*null*/ + 500 /*to make signals work*/;
  char *bytes = (char *)calloc(size, sizeof(char));
  int res = cliver_ktest_copy("select", select_index--, bytes, size);

  // Parse the recorded select input/output.
  char *recorded_select = bytes;
  char *item, *tmp;
  int ret = 0, recorded_ktest_nfds = 0, recorded_nfds = 0, active_fd_count = 0, i = 0;

  FD_ZERO(readfds); // output of select
  if(writefds != NULL){ FD_ZERO(writefds);}// output of select

  tmp = strtok(recorded_select, " ");

  assert(strcmp(tmp, "signal_indicator") == 0);
  int signal_indicator = atoi(strtok(NULL, " "));

  tmp = strtok(NULL, " ");
  assert(strcmp(tmp, "signal_val") == 0);
  int signal_val = atoi(strtok(NULL, " "));
  printf("ktest_select signal_indicator: %d signal_val %d\n", signal_indicator, signal_val);
  if(signal_indicator){
    printf("ktest_select calling signal_handler\n");
    signal_handler(signal_val);
  }

  tmp = strtok(NULL, " ");
  assert(strcmp(tmp, "ktest_nfds") == 0);
  recorded_ktest_nfds = atoi(strtok(NULL, " ")); // socket for TLS traffic
  printf("ktest_select ktest_nfds: %d recorded_ktest_nfds %d\n", ktest_nfds, recorded_ktest_nfds);
  assert(ktest_nfds == recorded_ktest_nfds);

  tmp = strtok(NULL, " ");
  assert(strcmp(tmp, "nfds") == 0);
  recorded_nfds = atoi(strtok(NULL, " "));


  // Copy recorded data to the final output fd_sets.
  FD_ZERO(readfds);
  if(writefds != NULL){ FD_ZERO(writefds);}

  tmp = strtok(NULL, " ");
  assert(strcmp(tmp, "outR") == 0);
  item = strtok(NULL, " ");
  assert(strlen(item) == (recorded_ktest_nfds + 3));

  //set readfds
  for (i = 0; i < 3; i++) { //0, 1, 2 set readfds
    if (item[i] == '1') {
      FD_SET(i, readfds);
      active_fd_count++;
    }
  }
  for (i = 0; i < recorded_ktest_nfds; i++) {
    if (item[i + 3] == '1') {  //ktest_sockfds set readfds
      FD_SET(ktest_sockfds[i], readfds);
      active_fd_count++;
    }
  }


  if(writefds != NULL) {
      tmp = strtok(NULL, " ");
      assert(strcmp(tmp, "outW") == 0);
      item = strtok(NULL, " ");
      assert(strlen(item) == (recorded_ktest_nfds + 3));
      //set writefds
      for (i = 0; i < 3; i++) { //0, 1, 2 set writefds
          if (item[i] == '1') {
              FD_SET(i, writefds);
              active_fd_count++;
          }
      }
      for (i = 0; i < recorded_ktest_nfds; i++) {
          if (item[i +3] == '1') { //ktest_sockfds set writefds
              FD_SET(ktest_sockfds[i], writefds);
              active_fd_count++;
          }
      }
  }


  tmp = strtok(NULL, " ");
  assert(strcmp(tmp, "active_fd_count") == 0);
  ret = atoi(strtok(NULL, " "));
  //Debug print
  printf("SELECT playback (recorded_nfds = %d, actual_nfds = %d):\n",
     (recorded_ktest_nfds + 3), nfds);
  printf("  outR:");
  print_fd_set((recorded_ktest_nfds+3), readfds);
  if(writefds != NULL) printf("  outW:");
  if(writefds != NULL) print_fd_set((recorded_ktest_nfds+3), writefds);
  printf("  ret = %d active_fd_count = %d\n", ret, active_fd_count);
  //end debug print

  if(ret != active_fd_count) printf("select ret = %d active_fd_count = %d\n", ret, active_fd_count);
  assert(active_fd_count == ret); // Did we miss anything?
  free(recorded_select);

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

DEFINE_MODEL(int, ktest_select_and_signal, int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, struct timeval *timeout) {
  printf("klee's ktest_select_and_signal calling signal_handler\n");
  int signal_indicator;
  klee_make_symbolic(&signal_indicator, sizeof(signal_indicator));
  if(signal_indicator){
    int signal_val;
    klee_make_symbolic(&signal_val, sizeof(signal_val));
    printf("klee's ktest_select_and_signal calling signal_handler\n");
    signal_handler(signal_val);
  }

  return ktest_select(nfds, readfds, writefds, exceptfds, timeout);
}


DEFINE_MODEL(int, ktest_select, int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, struct timeval *timeout) {
  assert(readfds != NULL);
  assert(exceptfds == NULL);
  //DEBUG_PRINT("symbolic");

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
      if (in_readfds.fds_bits[i] != 0) {
        fd_mask symbolic_mask;
        klee_make_symbolic(&symbolic_mask, sizeof(fd_mask));
        readfds->fds_bits[i] = in_readfds.fds_bits[i] & symbolic_mask;
        all_bits_or |= readfds->fds_bits[i];
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
    //If service is NULL, then the port number of the returned socket addresses
    //will be left uninitialized.
    if(port != NULL)
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
    if(flags ==F_GETFL)
        return 1;
    else if(flags == F_SETFL || flags == F_SETFD)
        return 0;
    else return -1;
}

DEFINE_MODEL(int, ktest_listen, int sockfd, int backlog){
    return 0;
}


