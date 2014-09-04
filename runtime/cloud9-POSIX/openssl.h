#ifndef OPENSSL_H_
#define OPENSSL_H_

#include "common.h"

// FIXME: need internal openssl data types
#include "../../../openssl/include/openssl/ssl.h"
#include "../../../openssl/include/openssl/ssl3.h"
#include "../../../openssl/include/openssl/sha.h"
#include "../../../openssl/include/openssl/ec.h"
#include "../../../openssl/include/openssl/ossl_typ.h"
#include "../../../openssl/include/openssl/modes.h"
#include "../../../openssl/crypto/ec/ec_lcl.h"
#include "../../../openssl/crypto/modes/modes_lcl.h"
#include "../../../openssl/crypto/sha/sha.h"

// Enable debug output
#define DEBUG_OPENSSL_MODEL 0

// Enable for fully concrete model (requires ktest)
#define KTEST_RAND_PLAYBACK 0
#define KTEST_SELECT_PLAYBACK 0
#define KTEST_STDIN_PLAYBACK 0

DECLARE_MODEL(int, RAND_bytes, unsigned char *buf, int num)
DECLARE_MODEL(int, RAND_pseudo_bytes, unsigned char *buf, int num)

DECLARE_MODEL(int, EC_KEY_generate_key, EC_KEY *eckey)
DECLARE_MODEL(int, ECDH_compute_key, void *out, size_t outlen, const EC_POINT *pub_key, EC_KEY *eckey, void *(*KDF)(const void *in, size_t inlen, void *out, size_t *outlen))
DECLARE_MODEL(int, tls1_generate_master_secret, SSL *s, unsigned char *out, unsigned char *p, int len)
DECLARE_MODEL(size_t, EC_POINT_point2oct, const EC_GROUP *group, const EC_POINT *point, point_conversion_form_t form, unsigned char *buf, size_t len, BN_CTX *ctx)

DECLARE_MODEL(int, SHA1_Update, SHA_CTX *c, const void *data, size_t len)
DECLARE_MODEL(int, SHA1_Final, unsigned char *md, SHA_CTX *c)
DECLARE_MODEL(int, SHA256_Update, SHA256_CTX *c, const void *data, size_t len)
DECLARE_MODEL(int, SHA256_Final, unsigned char *md, SHA256_CTX *c)

DECLARE_MODEL(int, ktest_select, int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, struct timeval *timeout)

DECLARE_MODEL(void, CRYPTO_gcm128_init, GCM128_CONTEXT *ctx, void *key, block128_f block)
DECLARE_MODEL(void, CRYPTO_gcm128_setiv, GCM128_CONTEXT *ctx,const unsigned char *iv,size_t len)
DECLARE_MODEL(int, CRYPTO_gcm128_aad, GCM128_CONTEXT *ctx,const unsigned char *aad,size_t len)
DECLARE_MODEL(int, CRYPTO_gcm128_encrypt, GCM128_CONTEXT *ctx, const unsigned char *in, unsigned char *out, size_t len)
DECLARE_MODEL(void, CRYPTO_gcm128_tag, GCM128_CONTEXT *ctx, unsigned char *tag, size_t len)
DECLARE_MODEL(int, CRYPTO_gcm128_finish, GCM128_CONTEXT *ctx,const unsigned char *tag, size_t len)

DECLARE_MODEL(int, EVP_CipherInit, EVP_CIPHER_CTX *ctx, const EVP_CIPHER *cipher, const unsigned char *key, const unsigned char *iv, int enc)
DECLARE_MODEL(int, EVP_Cipher, EVP_CIPHER_CTX *ctx, unsigned char *out, const unsigned char *in, unsigned int inl)
DECLARE_MODEL(int, EVP_CipherUpdate, EVP_CIPHER_CTX *ctx, unsigned char *out, int *outl, const unsigned char *in, int inl)
DECLARE_MODEL(int, EVP_CipherFinal, EVP_CIPHER_CTX *ctx, unsigned char *outm, int *outl)

DECLARE_MODEL(void, print_stuff, BIO *bio, SSL *s, int full)

#endif /* OPENSSL_H_ */
