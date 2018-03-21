#include "openssl.h"
#include <assert.h>


////////////////////////////////////////////////////////////////////////////////
// OpenSSH DH key generation
////////////////////////////////////////////////////////////////////////////////

#define MAX_LEN 1000
DEFINE_MODEL(DH*, ktest_verify_choose_dh, int min, int wantbits, int max){
  printf("klee's ktest_verify_choose_dh entered\n");

  printf("klee's ktest_verify_choose_dh calling writesocket with min %d, wantbits %d, max %d\n", min, wantbits, max);
  ktest_writesocket(monitor_socket, (char*)&min, sizeof(min));
  ktest_writesocket(monitor_socket, (char*)&wantbits, sizeof(wantbits));
  ktest_writesocket(monitor_socket, (char*)&max, sizeof(max));

  unsigned char *from = malloc(MAX_LEN);

  //recover p
  int len = ktest_readsocket(monitor_socket, (char*)from, MAX_LEN);
  BIGNUM *p = BN_bin2bn(from, len, NULL);

  //recover g
  len = ktest_readsocket(monitor_socket, (char*)from, MAX_LEN);
  BIGNUM *g = BN_bin2bn(from, len, NULL);

  free(from);

  return dh_new_group(g, p);

}


DEFINE_MODEL(int, ktest_verify_DH_generate_key, DH *dh){
  printf("klee's ktest_verify_DH_generate_key entered\n");
  //writing inputs
  unsigned char *to;
  int len = bn_to_buf(&to, dh->p);
  ktest_writesocket(monitor_socket, to, len);
  free(to);

  len = bn_to_buf(&to, dh->g);
  ktest_writesocket(monitor_socket, to, len);
  free(to);


  //If dh->priv_key is null, we don't need to send it, but we do need to read
  //it.
  int priv_was_null = -1;
  if(dh->priv_key != NULL){
    priv_was_null = 0;
    ktest_writesocket(monitor_socket, dh->priv_key->d, dh->priv_key->dmax);
  } else {
    priv_was_null = 1;
    ktest_writesocket(monitor_socket, NULL, 0);
  }

  //Reading results
  unsigned char *from = malloc(MAX_LEN);
  if(!priv_was_null){//Reading dh->priv_key if was null
    len = ktest_readsocket(monitor_socket, (char*)from, MAX_LEN);
    dh->priv_key = BN_bin2bn(from, len, NULL);
  }

  //Reading dh->pub_key
  len = ktest_readsocket(monitor_socket, (char*)from, MAX_LEN);
  dh->pub_key = BN_bin2bn(from, len, NULL);  
  free(from);

  int ret = -1;
  ktest_readsocket(monitor_socket, &ret, sizeof(ret));
  return ret;
}


DEFINE_MODEL(int, ktest_verify_DH_compute_key, unsigned char *key, BIGNUM *pub_key, DH *dh){
  printf("klee's test_verify_DH_compute_key entered\n");
  //send pub_key
  unsigned char *to;
  int len = bn_to_buf(&to, pub_key);
  ktest_writesocket(monitor_socket, to, len);
  free(to);

  //the parts of dh we need:
  len = bn_to_buf(&to, dh->p);
  ktest_writesocket(monitor_socket, to, len);
  free(to);

  len = bn_to_buf(&to, dh->g);
  ktest_writesocket(monitor_socket, to, len);
  free(to);

  len = bn_to_buf(&to, dh->priv_key);
  ktest_writesocket(monitor_socket, to, len);
  free(to);

  //From Man: key must point to DH_size(dh) bytes of memory.
  int ret = ktest_readsocket(monitor_socket, key, DH_size(dh));
  return ret;
}


DEFINE_MODEL(int, ktest_verify_RSA_sign, int type, const unsigned char *m, unsigned int m_len,
    unsigned char *sigret, unsigned int *siglen, RSA *rsa){
  printf("klee's ktest_verify_RSA_sign entered\n");
  //Send:  type, m (m_len)
  ktest_writesocket(monitor_socket, &type, sizeof(type));
  ktest_writesocket(monitor_socket, m, m_len);

  //Send important parts of rsa: n, d, p, q
  unsigned char *to;
  int len = bn_to_buf(&to, rsa->n);
  ktest_writesocket(monitor_socket, to, len);
  free(to);

  len = bn_to_buf(&to, rsa->d);
  ktest_writesocket(monitor_socket, to, len);
  free(to);

  len = bn_to_buf(&to, rsa->p);
  ktest_writesocket(monitor_socket, to, len);
  free(to);

  len = bn_to_buf(&to, rsa->q);
  ktest_writesocket(monitor_socket, to, len);
  free(to);

  //Return values: sig, siglen, ret
  *siglen = ktest_readsocket(monitor_socket, sigret, RSA_size(rsa));
  int ret = -1;
  ktest_readsocket(monitor_socket, &ret, sizeof(ret));
  return ret;
}
    
