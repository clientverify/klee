#include "openssl.h"
#include <assert.h>

#if DEBUG_OPENSSL_MODEL
//#define DEBUG_PRINT(x) printf("%s: %s\n", __FUNCTION__, x);
//#define DEBUG_PRINT(...) printf("%s: ", __FUNCTION__); printf(__VA_ARGS__); printf("\n");
//#define DEBUG_PRINT(x) klee_warning(x); klee_stack_trace();
#define DEBUG_PRINT(x) klee_warning(x);
#else
#define DEBUG_PRINT(x) 
#endif

//DEFINE_MODEL(void*, memset, void *s, int c, size_t n) {
//  return klee_memset(s, c, n);
//}

////////////////////////////////////////////////////////////////////////////////
// OpenSSL helper routines
////////////////////////////////////////////////////////////////////////////////

// Check if buffer is symbolic
static void* is_symbolic_buffer(const void* buf, int len) {
#if OPENSSL_SYMBOLIC_TAINT
  unsigned i;
  for (i=0; i<len; ++i) {
    if (klee_is_symbolic(*((unsigned char*)(buf)+i))) {
      return (unsigned char*)(buf)+i;
    }
  }
  return 0;
#else
  return klee_is_symbolic_buffer(buf, len);
#endif
}

// Check if BIGNUM is symbolic
static void* is_symbolic_BIGNUM(BIGNUM* bn) {
  void *retval = is_symbolic_buffer(bn->d, bn->dmax);
  if (!retval)
    retval = is_symbolic_buffer(&(bn->neg), sizeof(bn->neg));
  return retval;
}

// Check if EC_POINT is symbolic
static int is_symbolic_EC_POINT(EC_POINT* p) {
  void *retval = is_symbolic_BIGNUM(&(p->X));
  if (!retval)
    retval = is_symbolic_BIGNUM(&(p->Y));
  if (!retval)
    retval = is_symbolic_BIGNUM(&(p->Z));
  return retval;
}

// Allocate a temporary symbolic buffer and copy into buf; this is needed because
// klee_make_symbolic looks up the allocated object for a buf pointer and will fail
// on non-aligned pointers. If taint is non-null, it is used internally to track
// the flow of symbolic data through modelled functions
void copy_symbolic_buffer(unsigned char* buf, int len,
    char* tag, void* taint) {
  if (buf) {
    unsigned char* symbolic_buf = (unsigned char*)malloc(len);
#if OPENSSL_SYMBOLIC_TAINT
    if (taint != NULL)
      klee_make_symbolic(symbolic_buf, len, tag, *((unsigned char*)taint));
    else
      klee_make_symbolic(symbolic_buf, len, tag);
#else
    klee_make_symbolic(symbolic_buf, len, tag);
#endif
    memcpy(buf, symbolic_buf, len);
    free(symbolic_buf);
  }
}

#define SYMBOLIC_MODEL_CHECK_AND_RETURN_ALWAYS(out, outlen, tag, retval) \
  copy_symbolic_buffer(out, outlen, tag, NULL); \
  return retval;

#define SYMBOLIC_MODEL_CHECK_AND_RETURN(in, inlen, out, outlen, tag, retval) \
  do { \
    void *ptr = is_symbolic_buffer(in, inlen); \
    if (ptr) { \
      copy_symbolic_buffer(out, outlen, tag, ptr); \
      return retval; \
    } \
  } while(0);

#define SYMBOLIC_MODEL_CHECK_AND_RETURN_VOID(in, inlen, out, outlen, tag) \
  do { \
    void *ptr = is_symbolic_buffer(in, inlen); \
    if (ptr) { \
      copy_symbolic_buffer(out, outlen, tag, ptr); \
      return; \
    } \
  } while(0);

static void print_buffer(void *buf, size_t s, char* tag) {
#if DEBUG_OPENSSL_MODEL
  klee_print_bytes(tag, buf, s);
#endif
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
  SYMBOLIC_MODEL_CHECK_AND_RETURN_ALWAYS(buf, num, "rng", num);
}

DEFINE_MODEL(int, RAND_pseudo_bytes, unsigned char *buf, int num) {
#if KTEST_RAND_PLAYBACK
  static int prng_index = -1;
  DEBUG_PRINT("playback");
  return cliver_ktest_copy("prng", prng_index--, buf, num);
#endif
  SYMBOLIC_MODEL_CHECK_AND_RETURN_ALWAYS(buf, num, "prng", num);
}

DEFINE_MODEL(int, RAND_poll, void) {
  DEBUG_PRINT("stubbed");
  return 1;
}

////////////////////////////////////////////////////////////////////////////////
// ECDH key generation
////////////////////////////////////////////////////////////////////////////////

DEFINE_MODEL(int, EC_KEY_generate_key, EC_KEY *eckey) {

#if KTEST_RAND_PLAYBACK
  DEBUG_PRINT("concrete");
  return CALL_UNDERLYING(EC_KEY_generate_key, eckey);
#endif

  // Modeled to always execute symbolically
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

DEFINE_MODEL(int, ECDH_compute_key, void *out, size_t outlen, 
    const EC_POINT *pub_key, EC_KEY *eckey, 
    void *(*KDF)(const void *in, size_t inlen, void *out, size_t *outlen)) {

  void* symbyte = is_symbolic_EC_POINT(pub_key);
  if (!symbyte)
    symbyte = is_symbolic_EC_POINT(eckey->pub_key);
  if (!symbyte)
    symbyte = is_symbolic_BIGNUM(eckey->priv_key);

  if (symbyte) {
    DEBUG_PRINT("symbolic");
    copy_symbolic_buffer(out, outlen, "EDCH_compute_key_out", symbyte);
    return outlen;
  }
  DEBUG_PRINT("concrete");
  return CALL_UNDERLYING(ECDH_compute_key, out, outlen, pub_key, eckey, KDF);
}

DEFINE_MODEL(int, tls1_generate_master_secret, SSL *s, unsigned char *out, 
    unsigned char *p, int len) {
  if (is_symbolic_buffer(p, len)) {
    DEBUG_PRINT("playback");
    cliver_ktest_copy("master_secret", -1, out, SSL3_MASTER_SECRET_SIZE);
    return SSL3_MASTER_SECRET_SIZE;
  }
  DEBUG_PRINT("concrete");
  return CALL_UNDERLYING(tls1_generate_master_secret, s, out, p, len);
}

DEFINE_MODEL(size_t, EC_POINT_point2oct, const EC_GROUP *group, 
    const EC_POINT *point, point_conversion_form_t form, 
    unsigned char *buf, size_t len, BN_CTX *ctx) {

  size_t field_len = BN_num_bytes(&group->field);
  size_t ret = (form == POINT_CONVERSION_COMPRESSED) ? 1 + field_len : 1 + 2*field_len;

  SYMBOLIC_MODEL_CHECK_AND_RETURN(point,sizeof(EC_POINT),buf,ret,"point2oct",ret);
  return CALL_UNDERLYING(EC_POINT_point2oct, group, point, form, buf, len, ctx);
}

////////////////////////////////////////////////////////////////////////////////
// SHA1 / SHA256 
////////////////////////////////////////////////////////////////////////////////

DEFINE_MODEL(int, SHA1_Update, SHA_CTX *c, const void *data, size_t len) {
  SYMBOLIC_MODEL_CHECK_AND_RETURN(data, len, c, 20, "SHA1", 1);
  SYMBOLIC_MODEL_CHECK_AND_RETURN(c, 20, c, 20, "SHA1", 1);
  return CALL_UNDERLYING(SHA1_Update, c, data, len);
}

DEFINE_MODEL(int, SHA1_Final, unsigned char *md, SHA_CTX *c) {
  SYMBOLIC_MODEL_CHECK_AND_RETURN(c, 20, md, SHA_DIGEST_LENGTH, "SHA1FINAL", 1);
  return CALL_UNDERLYING(SHA1_Final, md, c);
}

DEFINE_MODEL(int, SHA256_Update, SHA256_CTX *c, const void *data, size_t len) {
  SYMBOLIC_MODEL_CHECK_AND_RETURN(data, len, c, 32, "SHA256", 1);
  SYMBOLIC_MODEL_CHECK_AND_RETURN(c, 32, c, 32, "SHA256", 1);
  return CALL_UNDERLYING(SHA256_Update, c, data, len);
}

DEFINE_MODEL(int, SHA256_Final, unsigned char *md, SHA256_CTX *c) {
  SYMBOLIC_MODEL_CHECK_AND_RETURN(c, 32, md, SHA_DIGEST_LENGTH, "SHA256FINAL", 1);
  return CALL_UNDERLYING(SHA256_Final, md, c);
}

////////////////////////////////////////////////////////////////////////////////
// GCM128 Encryption / Decryption
////////////////////////////////////////////////////////////////////////////////

DEFINE_MODEL(void, gcm_gmult_4bit, u64 Xi[2], const u128 Htable[16]) {
  SYMBOLIC_MODEL_CHECK_AND_RETURN_VOID(Xi, 16, Xi, 16, "gmult")
  CALL_UNDERLYING(gcm_gmult_4bit, Xi, Htable);
}

DEFINE_MODEL(void, AES_encrypt, const unsigned char *in, unsigned char *out, const AES_KEY *key) {
  SYMBOLIC_MODEL_CHECK_AND_RETURN_VOID(in, 16, out, 16, "AESBlock")
  SYMBOLIC_MODEL_CHECK_AND_RETURN_VOID(key, sizeof(AES_KEY), out, 16, "AESBlock")
  CALL_UNDERLYING(AES_encrypt, in, out, key);
}

DEFINE_MODEL(void, gcm_ghash_4bit, u64 Xi[2], const u128 Htable[16],const u8 *inp,size_t len) {
  SYMBOLIC_MODEL_CHECK_AND_RETURN_VOID(Xi, 16, Xi, 16, "ghash")
  SYMBOLIC_MODEL_CHECK_AND_RETURN_VOID(inp, len, Xi, 16, "ghash")
  CALL_UNDERLYING(gcm_ghash_4bit, Xi, Htable, inp, len);
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

#if IGNORE_STD_WRITES
// Hack: Can't use DEFINE_MODEL macro with variadic functions,
// so we define it explicitly here.
void __klee_model_printf(const char *format, ...) {
  return;
}

int __klee_model_putchar(int c) {
  return 1;
}
#endif
