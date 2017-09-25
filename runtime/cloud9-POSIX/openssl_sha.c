////////////////////////////////////////////////////////////////////////////////
// SHA1 / SHA256 
////////////////////////////////////////////////////////////////////////////////


//DEFINE_MODEL(int, SHA1_Update, SHA_CTX *c, const void *data, size_t len) {
//
//  char* byte = is_symbolic_buffer(data, len);
//  if (byte) {
//  //if (is_symbolic_buffer(data, len)) {
//    DEBUG_PRINT("symbolic");
//    //copy_symbolic_buffer(c, sizeof(SHA_CTX), "SHA1_CTX");
//    copy_symbolic_buffer_b(c, sizeof(SHA_CTX), "SHA1_CTX", byte);
//    return 1;
//  }
//
//  if (klee_is_symbolic(c->h0)) {
//    DEBUG_PRINT("symbolic");
//    return 1;
//  }
//
//  DEBUG_PRINT("concrete");
//  return CALL_UNDERLYING(SHA1_Update, c, data, len);
//}
//
//DEFINE_MODEL(int, SHA1_Final, unsigned char *md, SHA_CTX *c) {
//
//  char* byte = is_symbolic_buffer(&(c->h0), 1);
//  //if (klee_is_symbolic(c->h0)) {
//  if (byte) {
//    DEBUG_PRINT("symbolic");
//    //copy_symbolic_buffer(md, SHA_DIGEST_LENGTH, "SHA1_md");
//    copy_symbolic_buffer_b(md, SHA_DIGEST_LENGTH, "SHA1_md", byte);
//    return 1;
//  }
// 
//  DEBUG_PRINT("concrete");
//  return CALL_UNDERLYING(SHA1_Final, md, c);
//}

//DEFINE_MODEL(int, SHA256_Update, SHA256_CTX *c, const void *data, size_t len) {
//
//  char* byte = is_symbolic_buffer(data, len);
//  if (byte) {
//  //if (is_symbolic_buffer(data, len)) {
//    DEBUG_PRINT("symbolic data");
//    copy_symbolic_buffer_b(c, sizeof(SHA256_CTX), "SHA256_CTX", byte);
//    c->md_len = SHA256_DIGEST_LENGTH; 
//    return 1;
//  }
//
//  if (klee_is_symbolic(c->h[0])) {
//    DEBUG_PRINT("symbolic ctx");
//    return 1;
//  }
//
//  DEBUG_PRINT("concrete");
//  return CALL_UNDERLYING(SHA256_Update, c, data, len);
//}
//
//DEFINE_MODEL(int, SHA256_Final, unsigned char *md, SHA256_CTX *c) {
//
//  char* byte = is_symbolic_buffer(c->h, 1);
//  //if (klee_is_symbolic(c->h[0])) {
//  if (byte) {
//    DEBUG_PRINT("symbolic ctx");
//    //copy_symbolic_buffer(md, SHA256_DIGEST_LENGTH, "SHA256_md");
//    copy_symbolic_buffer_b(md, SHA256_DIGEST_LENGTH, "SHA256_md", byte);
//    return 1;
//  }
// 
//  DEBUG_PRINT("concrete");
//  return CALL_UNDERLYING(SHA256_Final, md, c);
//}


