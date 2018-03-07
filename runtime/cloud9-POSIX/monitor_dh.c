#include "openssl.h"
#include <assert.h>


////////////////////////////////////////////////////////////////////////////////
// OpenSSH DH key generation
////////////////////////////////////////////////////////////////////////////////

#define MAX_LEN 1000
DEFINE_MODEL(DH*, ktest_verify_choose_dh, int min, int wantbits, int max){
  printf("klee's ktest_verify_choose_dh entered\n");

  printf("klee's ktest_verify_choose_dh calling writesocket with min %d, wantbits %d, max %d\n", min, wantbits, max);
  ktest_writesocket(verification_socket, (char*)&min, sizeof(min));
  ktest_writesocket(verification_socket, (char*)&wantbits, sizeof(wantbits));
  ktest_writesocket(verification_socket, (char*)&max, sizeof(max));

  //dealing with the fact that the call this models has a single call to
  ktest_arc4random();

  unsigned char *from = malloc(MAX_LEN);

  //recover p
  int len = ktest_readsocket(verification_socket, (char*)from, MAX_LEN);
  BIGNUM *p = BN_bin2bn(from, len, NULL);

  //recover g
  len = ktest_readsocket(verification_socket, (char*)from, MAX_LEN);
  BIGNUM *g = BN_bin2bn(from, len, NULL);

  free(from);

  return dh_new_group(g, p);

}


DEFINE_MODEL(int, ktest_verify_DH_generate_key, DH *dh){
  printf("klee's ktest_verify_DH_generate_key entered\n");
  //writing inputs
  unsigned char *to;
  int len = bn_to_buf(&to, dh->p);
  ktest_writesocket(verification_socket, to, len);
  free(to);

  len = bn_to_buf(&to, dh->g);
  ktest_writesocket(verification_socket, to, len);
  free(to);


  //If dh->priv_key is null, we don't need to send it, but we do need to read
  //it.
  int priv_was_null = -1;
  if(dh->priv_key != NULL){
    priv_was_null = 0;
    len = bn_to_buf(&to, dh->priv_key);
    ktest_writesocket(verification_socket, to, len);
    free(to);
  } else {
    priv_was_null = 1;
    ktest_writesocket(verification_socket, NULL, 0);
  }

  //Reading results
  unsigned char *from = malloc(MAX_LEN);
  if(!priv_was_null){//Reading dh->priv_key if was null
    len = ktest_readsocket(verification_socket, (char*)from, MAX_LEN);
    dh->priv_key = BN_bin2bn(from, len, NULL);
  }

  //Reading dh->pub_key
  len = ktest_readsocket(verification_socket, (char*)from, MAX_LEN);
  dh->pub_key = BN_bin2bn(from, len, NULL);  
  free(from);

  int ret = -1;
  ktest_readsocket(verification_socket, &ret, sizeof(ret));
  return ret;
}
    
