#ifndef OPENSSL_H_
#define OPENSSL_H_

#include "common.h"
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>

// FIXME: need internal openssl data types
#include "../../../openssl/include/openssl/ssl.h"
#include "../../../openssl/include/openssl/evp.h"
#include "../../../openssl/include/openssl/ssl3.h"
#include "../../../openssl/include/openssl/sha.h"
#include "../../../openssl/include/openssl/ec.h"
#include "../../../openssl/include/openssl/ossl_typ.h"
// modes_lcl.h redfines objects included here
//#include "../../../openssl/include/openssl/modes.h"
#include "../../../openssl/crypto/ec/ec_lcl.h"
#include "../../../openssl/crypto/modes/modes_lcl.h"
#include "../../../openssl/crypto/aes/aes.h"
#include "../../../openssl/crypto/sha/sha.h"

// Track symbolic data flow through modelled functions
// see: copy_symbolic_buffer()
#define OPENSSL_SYMBOLIC_TAINT 0

// Ignore writes to stdout and stderr
#define IGNORE_STD_WRITES 1

// Enable debug output
#define DEBUG_OPENSSL_MODEL 0

// Enable for fully concrete model (requires ktest)
#define KTEST_RAND_PLAYBACK 0
#define KTEST_SELECT_PLAYBACK 0
#define KTEST_STDIN_PLAYBACK 0

// Enable for exercising fake padding
#define KTEST_STDIN_FAKE_PADDING 1

void copy_symbolic_buffer(unsigned char* buf, int len, char* tag, void* taint);

//DECLARE_MODEL(void*, memset, void *s, int c, size_t n)

DECLARE_MODEL(int, RAND_bytes, unsigned char *buf, int num)
DECLARE_MODEL(int, RAND_pseudo_bytes, unsigned char *buf, int num)
DECLARE_MODEL(int, RAND_poll, void)

DECLARE_MODEL(int, EC_KEY_generate_key, EC_KEY *eckey)
DECLARE_MODEL(int, ECDH_compute_key, void *out, size_t outlen, const EC_POINT *pub_key, EC_KEY *eckey, void *(*KDF)(const void *in, size_t inlen, void *out, size_t *outlen))
DECLARE_MODEL(int, tls1_generate_master_secret, SSL *s, unsigned char *out, unsigned char *p, int len)
DECLARE_MODEL(size_t, EC_POINT_point2oct, const EC_GROUP *group, const EC_POINT *point, point_conversion_form_t form, unsigned char *buf, size_t len, BN_CTX *ctx)

DECLARE_MODEL(int, SHA1_Update, SHA_CTX *c, const void *data, size_t len)
DECLARE_MODEL(int, SHA1_Final, unsigned char *md, SHA_CTX *c)
DECLARE_MODEL(int, SHA256_Update, SHA256_CTX *c, const void *data, size_t len)
DECLARE_MODEL(int, SHA256_Final, unsigned char *md, SHA256_CTX *c)

// KTest socket operations
DECLARE_MODEL(int, ktest_select, int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, struct timeval *timeout)
//DECLARE_MODEL(int, ktest_connect, int sockfd, const struct sockaddr *addr, socklen_t addrlen)
//DECLARE_MODEL(ssize_t, ktest_writesocket, int fd, const void *buf, size_t count)
//DECLARE_MODEL(ssize_t, ktest_readsocket, int fd, void *buf, size_t count)

// GCM128 Encryption / Decryption
//DECLARE_MODEL(void, CRYPTO_gcm128_init, GCM128_CONTEXT *ctx, void *key, block128_f block)
//DECLARE_MODEL(void, CRYPTO_gcm128_setiv, GCM128_CONTEXT *ctx,const unsigned char *iv,size_t len)
//DECLARE_MODEL(int, CRYPTO_gcm128_aad, GCM128_CONTEXT *ctx,const unsigned char *aad,size_t len)
//DECLARE_MODEL(int, CRYPTO_gcm128_encrypt, GCM128_CONTEXT *ctx, const unsigned char *in, unsigned char *out, size_t len)
//DECLARE_MODEL(void, CRYPTO_gcm128_tag, GCM128_CONTEXT *ctx, unsigned char *tag, size_t len)
//DECLARE_MODEL(int, CRYPTO_gcm128_finish, GCM128_CONTEXT *ctx,const unsigned char *tag, size_t len)
DECLARE_MODEL(void, gcm_gmult_4bit, u64 Xi[2], const u128 Htable[16])
DECLARE_MODEL(void, AES_encrypt, const unsigned char *in, unsigned char *out, const AES_KEY *key)
DECLARE_MODEL(void, gcm_ghash_4bit, u64 Xi[2], const u128 Htable[16], const u8 *inp,size_t len)

// EVP Cipher
DECLARE_MODEL(int, EVP_PKEY_verify, EVP_PKEY_CTX *ctx, const unsigned char *sig, size_t siglen, const unsigned char *tbs, size_t tbslen);
//DECLARE_MODEL(int, EVP_CIPHER_CTX_ctrl, EVP_CIPHER_CTX *ctx, int type, int arg, void *ptr)
//DECLARE_MODEL(int, EVP_CipherInit, EVP_CIPHER_CTX *ctx, const EVP_CIPHER *cipher, const unsigned char *key, const unsigned char *iv, int enc)
//DECLARE_MODEL(int, EVP_CipherInit_ex, EVP_CIPHER_CTX *ctx, const EVP_CIPHER *cipher, ENGINE *impl, const unsigned char *key, const unsigned char *iv, int enc)
//DECLARE_MODEL(int, EVP_Cipher, EVP_CIPHER_CTX *ctx, unsigned char *out, const unsigned char *in, unsigned int inl)
//DECLARE_MODEL(int, EVP_CipherUpdate, EVP_CIPHER_CTX *ctx, unsigned char *out, int *outl, const unsigned char *in, int inl)
//DECLARE_MODEL(int, EVP_CipherFinal, EVP_CIPHER_CTX *ctx, unsigned char *outm, int *outl)

// Private key for client authentication
DECLARE_MODEL(EVP_PKEY *, load_key, BIO *err, const char *file, int format, int maybe_stdin, const char *pass, ENGINE *e, const char *key_descrip)
DECLARE_MODEL(int, DH_compute_key, unsigned char *key, const BIGNUM *pub_key, DH *dh)

// Irrelevant output
DECLARE_MODEL(void, print_stuff, BIO *bio, SSL *s, int full)

#endif /* OPENSSL_H_ */
