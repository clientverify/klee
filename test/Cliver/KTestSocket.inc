
#ifndef KTEST

ssize_t ktest_send(int fd, const void *buf, size_t count) {
  return send(fd, buf, count, MSG_NOSIGNAL);
}

ssize_t ktest_recv(int fd, void *buf, size_t count, int flags) {
  ssize_t num_bytes = recv(fd, buf, count, flags);
}

void ktest_finish() {
  return;
}

#else

// Requires compilation with lib/Basic/KTest.cpp

KTestObject* ktest_objects = NULL;
int num_ktest_objects = -1;
int max_ktest_objects = 0;
enum { CLIENT_TO_SERVER=0, SERVER_TO_CLIENT=1 };
char* ktest_object_names[] = { "c2s", "s2c" };

void ktest_check_mem() {
  if (num_ktest_objects >= max_ktest_objects) { max_ktest_objects = (max_ktest_objects+1)*2;
    size_t size = max_ktest_objects * sizeof(KTestObject);
    ktest_objects = (KTestObject*) realloc(ktest_objects, size);
    if (!ktest_objects) {
      perror("ktest_check_mem error");
      exit(1);
    }
  }
}

ssize_t ktest_send(int fd, const void *buf, size_t count) {
  ssize_t num_bytes = send(fd, buf, count, MSG_NOSIGNAL);

  if (num_bytes > 0) {
    int i = ++num_ktest_objects;
    ktest_check_mem();
    ktest_objects[i].name = ktest_object_names[SERVER_TO_CLIENT];
    ktest_objects[i].numBytes = num_bytes;
    ktest_objects[i].bytes = (unsigned char*) malloc(sizeof (unsigned char) * num_bytes);
    memcpy(ktest_objects[i].bytes, buf, num_bytes);
  } else if (num_bytes < 0) {
    perror("ktest_send error");
    exit(1);
  }
  return num_bytes;
}

ssize_t ktest_recv(int fd, void *buf, size_t count, int flags) {
  ssize_t num_bytes = recv(fd, buf, count, flags);

  if (num_bytes > 0) {
    int i = ++num_ktest_objects;
    ktest_check_mem();
    ktest_objects[i].name = ktest_object_names[CLIENT_TO_SERVER];
    ktest_objects[i].numBytes = num_bytes;
    ktest_objects[i].bytes = (unsigned char*) malloc(sizeof (unsigned char) * num_bytes);
    memcpy(ktest_objects[i].bytes, buf, num_bytes);
  } else if (num_bytes < 0) {
    perror("ktest_recv error");
    exit(1);
  }
  return num_bytes;
}

void ktest_finish() {
  KTest ktest;
  memset(&ktest, 0, sizeof(KTest));
  ktest.numObjects = ++num_ktest_objects;
  ktest.objects = ktest_objects;

  //for (int i = 0; i<num_ktest_objects; i++) {
  //  printf("ktest_object[%d].name = %s\n", i, ktest_objects[i].name);
  //  printf("\t[%d]: ", ktest_objects[i].numBytes);
  //  for (int j=0; j<ktest_objects[i].numBytes; j++)
  //    printf("%x", ktest_objects[i].bytes[j]);
  //  printf("\n");
  //}

  int result = kTest_toFile(&ktest, KTEST);

  if (!result) {
    perror("ktest_finish error");
    exit(1);
  }
}

#endif

