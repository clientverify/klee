#include "openssl.h"
#include <assert.h>
#include <unistd.h>
 
#include "monitor_shared_state.h"

#include <string.h>
#include <security/pam_appl.h>
 
# define PAM_STRERROR(a,b) pam_strerror((a),(b))
# define PAM_MSG_MEMBER(msg, n, member) ((msg)[(n)]->member)
static pam_handle_t *pamh = NULL;
static char *__pam_msg = NULL;
static const char *__pampasswd = NULL;


//TODO: go back and make this record and playback
DEFINE_MODEL(int, ktest_verify_pamh_not_null, void){
  printf("klee's ktest_verify_pamh_not_null entered\n");

  int ret;
  ktest_readsocket(verification_socket, (char*)&ret, sizeof(ret));
  assert(ret == 0 || ret == 1);
  return ret;
}

DEFINE_MODEL(void, ktest_verify_set_password, char* password){
  ktest_writesocket(verification_socket, password, strlen(password)+1);
  __pampasswd = password;
  printf("klee's ktest_verify_set_password called with: %s\n",  password);
}

DEFINE_MODEL(char*, ktest_verify_pam_strerror, int ret_val){
  int MAX_LEN = 50;
  char* ret = malloc(MAX_LEN);
  ktest_readsocket(verification_socket, ret, MAX_LEN);
  printf("klee's ktest_verify_pam_strerror calling record_readbuf with ret %s\n", ret);
  return ret;
}


//Todo: record the arguements to this function in order to verify them.
//For all item_types, other than PAM_CONV and PAM_FAIL_DELAY, item is a pointer
//to a <NUL> terminated character string.
//In the case of PAM_CONV, item points to an initialized pam_conv structure. In
//the case of PAM_FAIL_DELAY, item is a function pointer: void (*delay_fn)(int
//retval, unsigned usec_delay, void *appdata_ptr)
DEFINE_MODEL(int, ktest_verify_pam_set_item, int item_type, const void *item){
  printf("klee's ktest_verify_pam_set_item entered\n");

  //For all item_types, other than PAM_CONV and PAM_FAIL_DELAY, item is a pointer
  //to a <NUL> terminated character string.
  if(item_type != PAM_CONV && item_type != PAM_FAIL_DELAY){
    const char* item_str = (const char*)item;
    printf("klee's ktest_verify_pam_set_item 1 writing: %s len: %d\n",
        item_str, strlen(item_str)+1);
    ktest_writesocket(verification_socket, item_str, strlen(item_str)+1);
    printf("klee's ktest_verify_pam_set_item 2\n");
    int ret = -1;
    ktest_readsocket(verification_socket, (char*)&ret, sizeof(ret));
    printf("klee's ktest_verify_pam_set_item 3\n");
    return ret;
  } else if (item_type == PAM_CONV){
    const char* item_str = (const char*)item;
    printf("klee's ktest_verify_pam_set_item 4\n");
    ktest_writesocket(verification_socket, item_str, strlen(item_str)+1);
    printf("klee's ktest_verify_pam_set_item 5\n");
    int ret = -1;
    ktest_readsocket(verification_socket, (char*)&ret, sizeof(ret));
    printf("klee's ktest_verify_pam_set_item 6\n");
    return ret;
  } else if (item_type != PAM_FAIL_DELAY){
    printf("klee's ktest_verify_pam_set_item 7\n");
    assert(0);
  }
}

//TODO: record the arguements to this function in order to verify them.
//This one will be rather challenging
DEFINE_MODEL(int, ktest_verify_pam_start, const char *service_name, const char *user){
  printf("klee's ktest_verify_pam_start entered\n");

  printf("ktest_verify_pam_start calling writesocket with service_name %s\n", service_name);
  ktest_writesocket(verification_socket, service_name, strlen(service_name));
  printf("ktest_verify_pam_start calling writesocket with user %s\n", user);
  ktest_writesocket(verification_socket, user, strlen(user));
  int ret = -1;
  ktest_readsocket(verification_socket, (char*)&ret, sizeof(ret));
  return ret;
}




DEFINE_MODEL(int, ktest_verify_pam_acct_mgmt, int flags){
  printf("klee's ktest_verify_pam_acct_mgmt entered\n");
  ktest_writesocket(verification_socket, (char*)&flags, sizeof(flags));
  int ret = -1;
  ktest_readsocket(verification_socket, (char*)&ret, sizeof(ret));
  return ret;
}

DEFINE_MODEL(int, ktest_verify_pam_setcred, int flags){
  printf("klee's ktest_verify_pam_setcred entered\n");
  ktest_writesocket(verification_socket, (char*)&flags, sizeof(flags));
  int ret = -1;
  ktest_readsocket(verification_socket, (char*)&ret, sizeof(ret));
  return ret;
}

DEFINE_MODEL(int, ktest_verify_pam_chauthtok, int flags){
  printf("klee's ktest_verify_pam_chauthtok entered\n");
  ktest_writesocket(verification_socket, (char*)&flags, sizeof(flags));
  int ret = -1;
  ktest_readsocket(verification_socket, (char*)&ret, sizeof(ret));
  return ret;
}

DEFINE_MODEL(int, ktest_verify_pam_open_session, int flags){
  printf("klee's ktest_verify_pam_open_session entered\n");
  ktest_writesocket(verification_socket, (char*)&flags, sizeof(flags));
  int ret = -1;
  ktest_readsocket(verification_socket, (char*)&ret, sizeof(ret));
  return ret;
}


DEFINE_MODEL(int, ktest_verify_pam_authenticate, int flags){
  printf("klee's ktest_verify_pam_authenticate entered\n");

  ktest_writesocket(verification_socket, (char*)&flags, sizeof(flags));
  int ret = -1;
  ktest_readsocket(verification_socket, (char*)&ret, sizeof(ret));
  return ret;
}

DEFINE_MODEL(int, ktest_verify_pam_close_session, int flags){
  printf("klee's ktest_verify_pam_close_session entered\n");
  ktest_writesocket(verification_socket, (char*)&flags, sizeof(flags));
  int ret = -1;
  ktest_readsocket(verification_socket, (char*)&ret, sizeof(ret));
  return ret;
}

DEFINE_MODEL(int, ktest_verify_pam_end, int flags){
  printf("klee's ktest_verify_pam_end entered\n");
  ktest_writesocket(verification_socket, &flags, sizeof(flags));
  int ret = -1;
  ktest_readsocket(verification_socket, &ret, sizeof(ret));
  return ret;
}
