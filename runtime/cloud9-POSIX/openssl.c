#include "openssl.h"
#include <assert.h>

#if DEBUG_OPENSSL_MODEL
//#define DEBUG_PRINT(x) printf("%s: %s\n", __FUNCTION__, x);
#define DEBUG_PRINT(x) klee_warning(x)
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
// OpenSSL helper routines
////////////////////////////////////////////////////////////////////////////////

// Check if buffer is symbolic
static void* is_symbolic_buffer(const void* buf, int len) {
  unsigned i;
  for (i=0; i<len; ++i) {
    if (klee_is_symbolic(*((unsigned char*)(buf)+i))) {
      return (unsigned char*)(buf)+i;
    }
  }
  return 0;
}

// Check if BIGNUM is symbolic
static int is_symbolic_BIGNUM(BIGNUM* bn) {
  return is_symbolic_buffer(bn->d, bn->dmax) || klee_is_symbolic(bn->neg);
}

// Check if EC_POINT is symbolic
static int is_symbolic_EC_POINT(EC_POINT* p) {
  return is_symbolic_BIGNUM(&(p->X)) || 
         is_symbolic_BIGNUM(&(p->Y)) || 
         is_symbolic_BIGNUM(&(p->Y));
}

// Allocate a temporary symbolic buffer and copy into buf; this is needed because
// klee_make_symbolic looks up the allocated object for a buf pointer and will fail
// on non-aligned pointers.
static void copy_symbolic_buffer(unsigned char* buf, int len, char* tag) {
  unsigned char* tmp_buf = (unsigned char*)malloc(len);
  klee_make_symbolic(tmp_buf, len, tag);
  memcpy(buf, tmp_buf, len);
  free(tmp_buf);
}

static void copy_symbolic_buffer_b(unsigned char* buf, int len, char* tag, void* b) {
  unsigned char* tmp_buf = (unsigned char*)malloc(len);
  klee_make_symbolic(tmp_buf, len, tag, *((unsigned char*)b));
  memcpy(buf, tmp_buf, len);
  free(tmp_buf);
}


// Make BIGNUM with internal symbolic data
#define SYMBOLIC_BN_DMAX 64
static void make_BN_symbolic(BIGNUM* bn) {
  if (bn->dmax > 0) {
    char *buf = (char *)malloc((bn->dmax)*sizeof(bn->d[0]));
    klee_make_symbolic(buf, (bn->dmax)*sizeof(bn->d[0]), "BN_buf");
    memcpy(bn->d, buf, bn->dmax);
    free(buf);
  } else {
    bn->dmax = SYMBOLIC_BN_DMAX;
    char *buf = (char *)malloc((bn->dmax)*sizeof(bn->d[0]));
    klee_make_symbolic(buf, (bn->dmax)*sizeof(bn->d[0]), "BN_buf");
    bn->d = buf;
  }
  int neg;
  klee_make_symbolic(&neg, sizeof(neg), "BN_neg");
  bn->neg = neg;
}

// Make EC_POINT symbolic 
static void make_EC_POINT_symbolic(EC_POINT* p) {
  make_BN_symbolic(&(p->X));
  make_BN_symbolic(&(p->Y));
  make_BN_symbolic(&(p->Z));
}

////////////////////////////////////////////////////////////////////////////////
// Random number generation
////////////////////////////////////////////////////////////////////////////////

DEFINE_MODEL(int, RAND_bytes, unsigned char *buf, int num) {
#if KTEST_RAND_PLAYBACK
  static int rng_index = -1;
  DEBUG_PRINT("playback");
  return cliver_ktest_copy("rng", rng_index--, buf, num);
#endif

  DEBUG_PRINT("symbolic");
  copy_symbolic_buffer(buf, num, "rng");
  return num;
}

DEFINE_MODEL(int, RAND_pseudo_bytes, unsigned char *buf, int num) {
#if KTEST_RAND_PLAYBACK
  static int prng_index = -1;
  DEBUG_PRINT("playback");
  return cliver_ktest_copy("prng", prng_index--, buf, num);
#endif

  DEBUG_PRINT("symbolic");
  copy_symbolic_buffer(buf, num, "prng");
  return num;
}

////////////////////////////////////////////////////////////////////////////////
// ECDH key generation
////////////////////////////////////////////////////////////////////////////////

DEFINE_MODEL(int, EC_KEY_generate_key, EC_KEY *eckey) {

#if KTEST_RAND_PLAYBACK
  DEBUG_PRINT("concrete");
  return CALL_UNDERLYING(EC_KEY_generate_key, eckey);
#endif

  DEBUG_PRINT("symbolic");
  BIGNUM *priv_key = eckey->priv_key;
  EC_POINT *pub_key = eckey->pub_key;

  if (priv_key == NULL)
    priv_key = BN_new();

  if (pub_key == NULL)
    pub_key = EC_POINT_new(eckey->group);

  make_BN_symbolic(priv_key);
  make_EC_POINT_symbolic(pub_key);

  eckey->priv_key = priv_key;
  eckey->pub_key = pub_key;

  return 1;
}

DEFINE_MODEL(int, ECDH_compute_key, void *out, size_t outlen, const EC_POINT *pub_key, EC_KEY *eckey, void *(*KDF)(const void *in, size_t inlen, void *out, size_t *outlen)) {
  if (is_symbolic_EC_POINT(pub_key) || 
      is_symbolic_EC_POINT(eckey->pub_key) || 
      is_symbolic_BIGNUM(eckey->priv_key)) {
    DEBUG_PRINT("symbolic");
    copy_symbolic_buffer(out, outlen, "EDCH_compute_key_out");
    return outlen;
  }
  DEBUG_PRINT("concrete");
  return CALL_UNDERLYING(ECDH_compute_key, out, outlen, pub_key, eckey, KDF);
}

DEFINE_MODEL(int, tls1_generate_master_secret, SSL *s, unsigned char *out, unsigned char *p, int len) {
  if (is_symbolic_buffer(p, len)) {
    DEBUG_PRINT("playback");
    cliver_ktest_copy("master_secret", -1, out, SSL3_MASTER_SECRET_SIZE);
    return SSL3_MASTER_SECRET_SIZE;
  }
  DEBUG_PRINT("concrete");
  return CALL_UNDERLYING(tls1_generate_master_secret, s, out, p, len);
}

DEFINE_MODEL(size_t, EC_POINT_point2oct, const EC_GROUP *group, const EC_POINT *point, point_conversion_form_t form, unsigned char *buf, size_t len, BN_CTX *ctx) {

  if (is_symbolic_buffer(point, sizeof(EC_POINT))) {
    DEBUG_PRINT("symbolic");
    size_t field_len = BN_num_bytes(&group->field);
    size_t ret = (form == POINT_CONVERSION_COMPRESSED) ? 1 + field_len : 1 + 2*field_len;

    if (buf != NULL)
      klee_make_symbolic(buf, ret, "EC_POINT_point2oct_buf");

    return ret;
  }
  DEBUG_PRINT("concrete");
  return CALL_UNDERLYING(EC_POINT_point2oct, group, point, form, buf, len, ctx);
}

////////////////////////////////////////////////////////////////////////////////
// SHA1 / SHA256 
////////////////////////////////////////////////////////////////////////////////

DEFINE_MODEL(int, SHA1_Update, SHA_CTX *c, const void *data, size_t len) {

  char* byte = is_symbolic_buffer(data, len);
  if (byte) {
  //if (is_symbolic_buffer(data, len)) {
    DEBUG_PRINT("symbolic");
    //copy_symbolic_buffer(c, sizeof(SHA_CTX), "SHA1_CTX");
    copy_symbolic_buffer_b(c, sizeof(SHA_CTX), "SHA1_CTX", byte);
    return 1;
  }

  if (klee_is_symbolic(c->h0)) {
    DEBUG_PRINT("symbolic");
    return 1;
  }

  DEBUG_PRINT("concrete");
  return CALL_UNDERLYING(SHA1_Update, c, data, len);
}

DEFINE_MODEL(int, SHA1_Final, unsigned char *md, SHA_CTX *c) {

  char* byte = is_symbolic_buffer(&(c->h0), 1);
  //if (klee_is_symbolic(c->h0)) {
  if (byte) {
    DEBUG_PRINT("symbolic");
    //copy_symbolic_buffer(md, SHA_DIGEST_LENGTH, "SHA1_md");
    copy_symbolic_buffer_b(md, SHA_DIGEST_LENGTH, "SHA1_md", byte);
    return 1;
  }
 
  DEBUG_PRINT("concrete");
  return CALL_UNDERLYING(SHA1_Final, md, c);
}

DEFINE_MODEL(int, SHA256_Update, SHA256_CTX *c, const void *data, size_t len) {

  char* byte = is_symbolic_buffer(data, len);
  if (byte) {
  //if (is_symbolic_buffer(data, len)) {
    DEBUG_PRINT("symbolic data");
    copy_symbolic_buffer_b(c, sizeof(SHA256_CTX), "SHA256_CTX", byte);
    c->md_len = SHA256_DIGEST_LENGTH; 
    return 1;
  }

  if (klee_is_symbolic(c->h[0])) {
    DEBUG_PRINT("symbolic ctx");
    return 1;
  }

  DEBUG_PRINT("concrete");
  return CALL_UNDERLYING(SHA256_Update, c, data, len);
}

DEFINE_MODEL(int, SHA256_Final, unsigned char *md, SHA256_CTX *c) {

  char* byte = is_symbolic_buffer(c->h, 1);
  //if (klee_is_symbolic(c->h[0])) {
  if (byte) {
    DEBUG_PRINT("symbolic ctx");
    //copy_symbolic_buffer(md, SHA256_DIGEST_LENGTH, "SHA256_md");
    copy_symbolic_buffer_b(md, SHA256_DIGEST_LENGTH, "SHA256_md", byte);
    return 1;
  }
 
  DEBUG_PRINT("concrete");
  return CALL_UNDERLYING(SHA256_Final, md, c);
}

////////////////////////////////////////////////////////////////////////////////
// select()
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
  static int select_index = -1;
  DEBUG_PRINT("playback");

  unsigned int size = 4*nfds + 40 /*text*/ + 3*4 /*3 fd's*/ + 1 /*null*/;
  char *bytes = (char *)calloc(size, sizeof(char));
  int res = cliver_ktest_copy("select", select_index--, bytes, size);
  klee_warning(bytes);
  printf("bytes: %s, size: %d, res: %d\n", bytes, size, res);

  // Parse the recorded select input/output.
  char *recorded_select = bytes;
  char *item;
  fd_set in_readfds, in_writefds, out_readfds, out_writefds;
  int i, ret, recorded_sockfd, recorded_nfds;

  FD_ZERO(&in_readfds);  // input to select
  FD_ZERO(&in_writefds); // input to select
  FD_ZERO(&out_readfds); // output of select
  FD_ZERO(&out_writefds);// output of select

  assert(strcmp(strtok(recorded_select, " "), "sockfd") == 0);
  recorded_sockfd = atoi(strtok(NULL, " ")); // socket for TLS traffic
  assert(strcmp(strtok(NULL, " "), "nfds") == 0);
  recorded_nfds = atoi(strtok(NULL, " "));
  assert(strcmp(strtok(NULL, " "), "inR") == 0);
  item = strtok(NULL, " ");
  assert(strlen(item) == recorded_nfds);
  for (i = 0; i < recorded_nfds; i++) {
    if (item[i] == '1') {
      FD_SET(i, &in_readfds);
    }
  }
  assert(strcmp(strtok(NULL, " "), "inW") == 0);
  item = strtok(NULL, " ");
  assert(strlen(item) == recorded_nfds);
  for (i = 0; i < recorded_nfds; i++) {
    if (item[i] == '1') {
      FD_SET(i, &in_writefds);
    }
  }
  assert(strcmp(strtok(NULL, " "), "ret") == 0);
  ret = atoi(strtok(NULL, " "));
  assert(strcmp(strtok(NULL, " "), "outR") == 0);
  item = strtok(NULL, " ");
  assert(strlen(item) == recorded_nfds);
  for (i = 0; i < recorded_nfds; i++) {
    if (item[i] == '1') {
      FD_SET(i, &out_readfds);
    }
  }
  assert(strcmp(strtok(NULL, " "), "outW") == 0);
  item = strtok(NULL, " ");
  assert(strlen(item) == recorded_nfds);
  for (i = 0; i < recorded_nfds; i++) {
    if (item[i] == '1') {
      FD_SET(i, &out_writefds);
    }
  }
  free(recorded_select);

  printf("SELECT playback (recorded_nfds = %d, actual_nfds = %d):\n",
          recorded_nfds, nfds);
  printf("  inR: ");
  print_fd_set(recorded_nfds, &in_readfds);
  printf("  inW: ");
  print_fd_set(recorded_nfds, &in_writefds);
  printf("  outR:");
  print_fd_set(recorded_nfds, &out_readfds);
  printf("  outW:");
  print_fd_set(recorded_nfds, &out_writefds);
  printf("  ret = %d\n", ret);

  // Copy recorded data to the final output fd_sets.
  FD_ZERO(readfds);
  FD_ZERO(writefds);
  int active_fd_count = 0;
  // stdin(0), stdout(1), stderr(2)
  for (i = 0; i < 3; i++) {
    if (FD_ISSET(i, &out_readfds)) {
      FD_SET(i, readfds);
      active_fd_count++;
    }
    if (FD_ISSET(i, &out_writefds)) {
      FD_SET(i, writefds);
      active_fd_count++;
    }
  }
  // TLS socket (nfds-1)
  if (FD_ISSET(recorded_sockfd, &out_readfds)) {
    //FD_SET(ktest_sockfd, readfds);
    FD_SET(nfds-1, readfds);
    active_fd_count++;
  }
  if (FD_ISSET(recorded_sockfd, &out_writefds)) {
    //FD_SET(ktest_sockfd, writefds);
    FD_SET(nfds-1, writefds);
    active_fd_count++;
  }
  assert(active_fd_count == ret); // Did we miss anything?

  return ret;
}

#else

DEFINE_MODEL(int, ktest_select, int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, struct timeval *timeout) {
  DEBUG_PRINT("symbolic");

  int i, retval, ret;
  fd_set in_readfds, in_writefds;

  // Copy the read and write fd_sets
  in_readfds = *readfds;
  in_writefds = *writefds;

  // Reset for output
  FD_ZERO(readfds);
  FD_ZERO(writefds);

  int mask_count = nfds/NFDBITS;
  for (i = 0; i <= mask_count; ++i) {
    if (in_readfds.fds_bits[i] != 0) {
      fd_mask symbolic_mask;
      klee_make_symbolic(&symbolic_mask, sizeof(fd_mask), "select_readfds");
      readfds->fds_bits[i] = in_readfds.fds_bits[i] & symbolic_mask;
    }

    if (in_writefds.fds_bits[i] != 0) {
      fd_mask symbolic_mask;
      klee_make_symbolic(&symbolic_mask, sizeof(fd_mask), "select_writefds");
      writefds->fds_bits[i] = in_writefds.fds_bits[i] & symbolic_mask;
    }
  }

  klee_make_symbolic(&retval, sizeof(retval), "select_retval");
  return retval;
}

#endif

////////////////////////////////////////////////////////////////////////////////
// GCM128 Encryption / Decryption
////////////////////////////////////////////////////////////////////////////////

DEFINE_MODEL(void, CRYPTO_gcm128_init, GCM128_CONTEXT *ctx, void *key, block128_f block) {

  void *symbolic_key_ptr = is_symbolic_buffer(key, 1);
  if (symbolic_key_ptr) {
    DEBUG_PRINT("symbolic");
    memset(ctx,0,sizeof(*ctx));
    ctx->block = block;
    ctx->key   = key;
    copy_symbolic_buffer_b(ctx->H.c, 16, "gcm128_init", symbolic_key_ptr);
    return;
  }

  DEBUG_PRINT("concrete");
  CALL_UNDERLYING(CRYPTO_gcm128_init, ctx, key, block);
}

DEFINE_MODEL(void, CRYPTO_gcm128_setiv, GCM128_CONTEXT *ctx,const unsigned char *iv,size_t len) {

  void* symbolic_iv_ptr = is_symbolic_buffer(iv, len);
  void* symbolic_ctx_ptr = is_symbolic_buffer(ctx->H.c, 16);

  if (symbolic_iv_ptr || symbolic_ctx_ptr) {
    DEBUG_PRINT("symbolic");
    if (symbolic_iv_ptr)
      copy_symbolic_buffer_b(ctx->H.c, 16, "gcm128_setiv", symbolic_iv_ptr);
    return;
  }

  DEBUG_PRINT("concrete");
  CALL_UNDERLYING(CRYPTO_gcm128_setiv, ctx, iv, len);
}

DEFINE_MODEL(int, CRYPTO_gcm128_aad, GCM128_CONTEXT *ctx,const unsigned char *aad,size_t len) {

  void *symbolic_aad_ptr = is_symbolic_buffer(aad, len);
  void *symbolic_ctx_ptr = is_symbolic_buffer(ctx->H.c, 16);

  if (symbolic_aad_ptr || symbolic_ctx_ptr) {
    DEBUG_PRINT("symbolic");
    if (symbolic_aad_ptr)
      copy_symbolic_buffer_b(ctx->H.c, 16, "gcm128_aad", symbolic_aad_ptr);
    return 0;
  }

  DEBUG_PRINT("concrete");
  return CALL_UNDERLYING(CRYPTO_gcm128_aad, ctx, aad, len);
}

DEFINE_MODEL(int, CRYPTO_gcm128_encrypt, GCM128_CONTEXT *ctx, const unsigned char *in, unsigned char *out, size_t len) {
  void *symbolic_in_ptr = is_symbolic_buffer(in, len);
  void *symbolic_ctx_ptr = is_symbolic_buffer(ctx->H.c, 16);

  if (symbolic_in_ptr || symbolic_ctx_ptr) {
    DEBUG_PRINT("symbolic");
    void *symbolic_ptr = symbolic_in_ptr ? symbolic_in_ptr : symbolic_ctx_ptr;
    copy_symbolic_buffer_b(out, len, "gcm128_encrypt_out", symbolic_ptr);
    return 0;
  }

  DEBUG_PRINT("concrete");
  return CALL_UNDERLYING(CRYPTO_gcm128_encrypt, ctx, in, out, len);
}

DEFINE_MODEL(int, CRYPTO_gcm128_finish, GCM128_CONTEXT *ctx, const unsigned char *tag, size_t len) {
  void *symbolic_ctx_ptr = is_symbolic_buffer(ctx->H.c, 16);
  if (symbolic_ctx_ptr) {
    DEBUG_PRINT("symbolic");
    // From openssl/crypto/modes/gcm128.c:CRYPTO_gcm128_tag()
    unsigned tag_size = len<=sizeof(ctx->Xi.c)?len:sizeof(ctx->Xi.c);
    copy_symbolic_buffer_b(ctx->Xi.c, tag_size, "gcm128_finish_Xic", symbolic_ctx_ptr);
    if (tag) {
      return memcmp(tag, ctx->Xi.c, tag_size);
    }
    return -1;
  }
  DEBUG_PRINT("concrete");
  return CALL_UNDERLYING(CRYPTO_gcm128_finish, ctx, tag, len);
}

DEFINE_MODEL(void, CRYPTO_gcm128_tag, GCM128_CONTEXT *ctx, unsigned char *tag, size_t len) {
  void *symbolic_ctx_ptr = is_symbolic_buffer(ctx->H.c, 16);
  if (symbolic_ctx_ptr) {
    DEBUG_PRINT("symbolic");
    // From openssl/crypto/modes/gcm128.c:CRYPTO_gcm128_tag()
    unsigned tag_size = len<=sizeof(ctx->Xi.c)?len:sizeof(ctx->Xi.c);
    copy_symbolic_buffer_b(ctx->Xi.c, tag_size, "gcm128_tag_Xic", symbolic_ctx_ptr);
    if (tag) 
      memcpy(tag, ctx->Xi.c, tag_size);
    return;
  }
  DEBUG_PRINT("concrete");
  return CALL_UNDERLYING(CRYPTO_gcm128_tag, ctx, tag, len);
}

////////////////////////////////////////////////////////////////////////////////
// EVP Cipher
////////////////////////////////////////////////////////////////////////////////

DEFINE_MODEL(int, EVP_CipherInit, EVP_CIPHER_CTX *ctx, const EVP_CIPHER *cipher, const unsigned char *key, const unsigned char *iv, int enc) {
  DEBUG_PRINT("concrete");
  return CALL_UNDERLYING(EVP_CipherInit, ctx, cipher, key, iv, enc);
}

DEFINE_MODEL(int, EVP_Cipher, EVP_CIPHER_CTX *ctx, unsigned char *out, const unsigned char *in, unsigned int inl) {
  DEBUG_PRINT("concrete");
  //klee_stack_trace();

  //EVP_AES_GCM_CTX *gctx = ctx->cipher_data;
  //void* symbolic_iv_ptr = is_symbolic_buffer(&ctx->iv, EVP_MAX_IV_LENGTH);
  //void* symbolic_oiv_ptr = is_symbolic_buffer(&ctx->oiv, EVP_MAX_IV_LENGTH);
  //void* symbolic_buf_ptr = is_symbolic_buffer(&ctx->buf, EVP_MAX_BLOCK_LENGTH);
  //void* symbolic_final_ptr = is_symbolic_buffer(&ctx->final, EVP_MAX_BLOCK_LENGTH);
  //if (symbolic_iv_ptr || symbolic_oiv_ptr || symbolic_buf_ptr || symbolic_final_ptr)
  //if (!ctx->encrypt)

  return CALL_UNDERLYING(EVP_Cipher, ctx, out, in, inl);
}

DEFINE_MODEL(int, EVP_CipherUpdate, EVP_CIPHER_CTX *ctx, unsigned char *out, int *outl, const unsigned char *in, int inl) {
  DEBUG_PRINT("concrete");
  return CALL_UNDERLYING(EVP_CipherUpdate, ctx, out, outl, in, inl);
}

DEFINE_MODEL(int, EVP_CipherFinal, EVP_CIPHER_CTX *ctx, unsigned char *outm, int *outl) {
  DEBUG_PRINT("concrete");
  return CALL_UNDERLYING(EVP_CipherFinal, ctx, outm, outl);
}

////////////////////////////////////////////////////////////////////////////////
// Irrelevant output
////////////////////////////////////////////////////////////////////////////////

DEFINE_MODEL(void, print_stuff, BIO *bio, SSL *s, int full) {
  DEBUG_PRINT("skipped");
  // If you ever want to actually call it, do:
  // CALL_UNDERLYING(print_stuff, bio, s, full)
  return;
}
