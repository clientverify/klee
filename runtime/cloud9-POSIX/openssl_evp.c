
#include "openssl.h"

////////////////////////////////////////////////////////////////////////////////
// EVP Cipher
////////////////////////////////////////////////////////////////////////////////

DEFINE_MODEL(int, EVP_PKEY_verify, EVP_PKEY_CTX *ctx,
             const unsigned char *sig, size_t siglen,
             const unsigned char *tbs, size_t tbslen) {
  return 1;
}

#if 0
DEFINE_MODEL(int, EVP_CIPHER_CTX_ctrl, EVP_CIPHER_CTX *ctx, int type, int arg, void *ptr) {

  //klee_debug("EVP_CIPHER_CTX_ctrl: type: %x", type);
  DEBUG_PRINT("concrete");
  return CALL_UNDERLYING(EVP_CIPHER_CTX_ctrl, ctx, type, arg, ptr);
}

DEFINE_MODEL(int, EVP_CipherInit_ex, EVP_CIPHER_CTX *ctx, const EVP_CIPHER *cipher, ENGINE *impl, const unsigned char *key, const unsigned char *iv, int enc) {
  if (ctx != NULL) {
    DEBUG_PRINT("not null");
    if (key) {
      //void* symbolic_key_ptr = is_symbolic_buffer(key, ctx->key_len);
      //if (symbolic_key_ptr) {
        print_buffer(key, ctx->key_len, "init key");
      //}
    } else {
      DEBUG_PRINT("null key");
    }
    if (iv) {
      //void* symbolic_iv_ptr = is_symbolic_buffer(iv, EVP_MAX_IV_LENGTH);
      //if (symbolic_iv_ptr) {
        print_buffer(iv, EVP_MAX_IV_LENGTH,"init iv");
      //}
    } else {
      DEBUG_PRINT("null iv");
    }
  }
  DEBUG_PRINT("concrete");
  //klee_interactive_wait();
  return CALL_UNDERLYING(EVP_CipherInit_ex, ctx, cipher, impl, key, iv, enc);
}

DEFINE_MODEL(int, EVP_CipherInit, EVP_CIPHER_CTX *ctx, const EVP_CIPHER *cipher, const unsigned char *key, const unsigned char *iv, int enc) {
  //void* symbolic_key_ptr = is_symbolic_buffer(&key, ctx->key_len);
  //void* symbolic_iv_ptr = is_symbolic_buffer(&iv, EVP_MAX_IV_LENGTH);
  //printf("EVP_CipherInit: key_len=%d\n", ctx->key_len);
  //printf("EVP_CipherInit: iv_len=%d\n", EVP_MAX_IV_LENGTH);
  DEBUG_PRINT("concrete");
  klee_interactive_wait();
  return CALL_UNDERLYING(EVP_CipherInit, ctx, cipher, key, iv, enc);
}

DEFINE_MODEL(int, EVP_Cipher, EVP_CIPHER_CTX *ctx, unsigned char *out, const unsigned char *in, unsigned int inl) {

  void *symbolic_ptr=NULL;
  void *symbolic_in_ptr=NULL, *symbolic_out_ptr=NULL, *symbolic_iv_ptr=NULL, 
       *symbolic_oiv_ptr=NULL, *symbolic_buf_ptr=NULL, *symbolic_final_ptr=NULL;

  if (ctx->encrypt) {
    DEBUG_PRINT("ENCRYPT=1");

    size_t in_len = inl;
    print_buffer(in, in_len, "in");
    //print_buffer(out, in_len, "out");
    //print_buffer(&ctx->iv, EVP_MAX_IV_LENGTH, "ctx->iv");
    //print_buffer(&ctx->oiv, EVP_MAX_IV_LENGTH, "ctx->oiv");
    //print_buffer(&ctx->buf, EVP_MAX_BLOCK_LENGTH, "ctx->buf");
    //print_buffer(&ctx->final, EVP_MAX_BLOCK_LENGTH, "ctx->final");

  } else {
    //DEBUG_PRINT("ENCRYPT=0");
  }
  //if (ctx->encrypt) {
  //  symbolic_ptr = symbolic_in_ptr = is_symbolic_buffer(in, inl);
  //  if (symbolic_ptr) {
  //    DEBUG_PRINT("symbolic");
  //    DEBUG_PRINT("calling EVP_CIPHER_CTX_ctrl(EVP_CTRL_INIT)");
  //    EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_INIT, 0, NULL);
  //    DEBUG_PRINT("calling EVP_CIPHER_CTX_ctrl(EVP_CTRL_GCM_IV_GEN)");
  //    EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_IV_GEN, EVP_GCM_TLS_EXPLICIT_IV_LEN, out);

  //    print_buffer(&(ctx->iv[4]), 8, "ctx->iv[4]");
  //    
  //    memcpy(out, &(ctx->iv[4]), 8);
  //    //copy_symbolic_buffer_b(out, inl, "EVP_Cipher", symbolic_ptr);
  //    copy_symbolic_buffer(out+8, inl-8, "EVP_Cipher");
  //    print_buffer(out, inl, "out");
  //    return inl;
  //  } else {
  //    DEBUG_PRINT("concrete");
  //  }
  //}


  ////EVP_AES_GCM_CTX *gctx = ctx->cipher_data;
  //if (ctx->encrypt)
  //  klee_warning("EVP_Cipher: ENCRYPT=1");
  //else
  //  klee_warning("EVP_Cipher: ENCRYPT=0");

  //klee_warning("EVP_Cipher: checking in");
  //symbolic_ptr = symbolic_in_ptr = is_symbolic_buffer(in+8, inl-24);

  //klee_warning("EVP_Cipher: checking out");
  //if (in != out)
  //  symbolic_ptr = symbolic_out_ptr = is_symbolic_buffer(out+8, inl-24);

  //klee_warning("EVP_Cipher: checking iv");
  //if (!symbolic_in_ptr) {
  //  symbolic_ptr = symbolic_iv_ptr = is_symbolic_buffer(&ctx->iv, EVP_MAX_IV_LENGTH);
  //  klee_warning("making iv symbolic");
  //  //copy_symbolic_buffer_b(&ctx->iv, EVP_MAX_IV_LENGTH, "EVP_Cipher_IV", symbolic_iv_ptr);
  //  bzero(&(ctx->iv), EVP_MAX_IV_LENGTH);
  //}

  ////klee_warning("EVP_Cipher: checking oiv");
  ////if (!symbolic_in_ptr && !symbolic_iv_ptr)
  ////  symbolic_ptr = symbolic_oiv_ptr = is_symbolic_buffer(&ctx->oiv, EVP_MAX_IV_LENGTH);

  ////klee_warning("EVP_Cipher: checking buf");
  ////if (!symbolic_in_ptr && !symbolic_iv_ptr && !symbolic_oiv_ptr)
  ////  symbolic_ptr = symbolic_buf_ptr = is_symbolic_buffer(&ctx->buf, EVP_MAX_BLOCK_LENGTH);

  ////klee_warning("EVP_Cipher: checking final");
  ////if (!symbolic_in_ptr && !symbolic_iv_ptr && !symbolic_oiv_ptr && !symbolic_buf_ptr)
  ////  symbolic_ptr = symbolic_final_ptr = is_symbolic_buffer(&ctx->final, EVP_MAX_BLOCK_LENGTH);

  //if (symbolic_in_ptr || symbolic_out_ptr || symbolic_iv_ptr || symbolic_oiv_ptr || symbolic_buf_ptr || symbolic_final_ptr) {
  //  //DEBUG_PRINT("symbolic: iv: %x, oiv: %x, buf: %x, final: %x\n", symbolic_iv_ptr, symbolic_oiv_ptr, symbolic_buf_ptr, symbolic_final_ptr);
  //  DEBUG_PRINT("symbolic");
  //  copy_symbolic_buffer_b(out, inl, "EVP_Cipher", symbolic_ptr);
  //  return inl;

  //} else {
  //  DEBUG_PRINT("concrete");
  //}

  //klee_interactive_wait();
  DEBUG_PRINT("concrete");
  int ures = CALL_UNDERLYING(EVP_Cipher, ctx, out, in, inl);

  if (ctx->encrypt) {
    DEBUG_PRINT("ENCRYPT=1 (after)");

    size_t in_len = inl;
    print_buffer(in, in_len, "in (after)");
    //print_buffer(out, in_len, "out (after)");
    //print_buffer(&ctx->iv, EVP_MAX_IV_LENGTH, "ctx->iv (after)");
    //print_buffer(&ctx->oiv, EVP_MAX_IV_LENGTH, "ctx->oiv (after)");
    //print_buffer(&ctx->buf, EVP_MAX_BLOCK_LENGTH, "ctx->buf (after)");
    //print_buffer(&ctx->final, EVP_MAX_BLOCK_LENGTH, "ctx->final (after)");
  }
  return ures;
}

DEFINE_MODEL(int, EVP_CipherUpdate, EVP_CIPHER_CTX *ctx, unsigned char *out, int *outl, const unsigned char *in, int inl) {
  //void* symbolic_iv_ptr = is_symbolic_buffer(&ctx->iv, EVP_MAX_IV_LENGTH);
  //void* symbolic_oiv_ptr = is_symbolic_buffer(&ctx->oiv, EVP_MAX_IV_LENGTH);
  //void* symbolic_buf_ptr = is_symbolic_buffer(&ctx->buf, EVP_MAX_BLOCK_LENGTH);
  //void* symbolic_final_ptr = is_symbolic_buffer(&ctx->final, EVP_MAX_BLOCK_LENGTH);
  //if (symbolic_iv_ptr || symbolic_oiv_ptr || symbolic_buf_ptr || symbolic_final_ptr) {
  //  //DEBUG_PRINT("symbolic: iv: %x, oiv: %x, buf: %x, final: %x\n", symbolic_iv_ptr, symbolic_oiv_ptr, symbolic_buf_ptr, symbolic_final_ptr);
  //  DEBUG_PRINT("symbolic");
  //}

  //klee_interactive_wait();
  DEBUG_PRINT("concrete");
  return CALL_UNDERLYING(EVP_CipherUpdate, ctx, out, outl, in, inl);
}

DEFINE_MODEL(int, EVP_CipherFinal, EVP_CIPHER_CTX *ctx, unsigned char *outm, int *outl) {
  //void* symbolic_iv_ptr = is_symbolic_buffer(&ctx->iv, EVP_MAX_IV_LENGTH);
  //void* symbolic_oiv_ptr = is_symbolic_buffer(&ctx->oiv, EVP_MAX_IV_LENGTH);
  //void* symbolic_buf_ptr = is_symbolic_buffer(&ctx->buf, EVP_MAX_BLOCK_LENGTH);
  //void* symbolic_final_ptr = is_symbolic_buffer(&ctx->final, EVP_MAX_BLOCK_LENGTH);
  //if (symbolic_iv_ptr || symbolic_oiv_ptr || symbolic_buf_ptr || symbolic_final_ptr) {
  //  //DEBUG_PRINT("symbolic: iv: %x, oiv: %x, buf: %x, final: %x\n", symbolic_iv_ptr, symbolic_oiv_ptr, symbolic_buf_ptr, symbolic_final_ptr);
  //  DEBUG_PRINT("symbolic");
  //}


  //klee_interactive_wait();
  DEBUG_PRINT("concrete");
  return CALL_UNDERLYING(EVP_CipherFinal, ctx, outm, outl);
}
#endif


