#if 0
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

  //printf("SETIV: len=%d\n", len);
  //print_buffer(iv, len, "iv");

  if (symbolic_iv_ptr || symbolic_ctx_ptr) {
    DEBUG_PRINT(symbolic_iv_ptr ? "symbolic iv": "symbolic ctx");
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
    //DEBUG_PRINT("symbolic");
    //void *symbolic_ptr = symbolic_in_ptr ? symbolic_in_ptr : symbolic_ctx_ptr;
    //copy_symbolic_buffer_b(out, len, "gcm128_encrypt_out", symbolic_ptr);
    //return 0;
    DEBUG_PRINT("symbolic, calling concrete anyway");
  }

  DEBUG_PRINT("concrete");
  print_buffer(in, len, "in (before encrypt)");
  int result = CALL_UNDERLYING(CRYPTO_gcm128_encrypt, ctx, in, out, len);
  print_buffer(in, len, "in (after encrypt)");
  return result;
  //return CALL_UNDERLYING(CRYPTO_gcm128_encrypt, ctx, in, out, len);
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
#endif

