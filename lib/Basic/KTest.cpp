//===-- KTest.cpp ---------------------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "klee/Internal/ADT/KTest.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <unistd.h>
#include <stdint.h>
#include <sys/time.h>

//Changed for TASE
#define KTEST_VERSION 4
#define KTEST_MAGIC_SIZE 5
#define KTEST_MAGIC "KTEST"
#include <cstdlib>

// for compatibility reasons
#define BOUT_MAGIC "BOUT\n"

/***/

static int read_uint32(FILE *f, unsigned *value_out) {
  unsigned char data[4];
  if (fread(data, 4, 1, f)!=1)
    return 0;
  *value_out = (((((data[0]<<8) + data[1])<<8) + data[2])<<8) + data[3];
  return 1;
}

static int write_uint32(FILE *f, unsigned value) {
  unsigned char data[4];
  data[0] = value>>24;
  data[1] = value>>16;
  data[2] = value>> 8;
  data[3] = value>> 0;
  return fwrite(data, 1, 4, f)==4;
}

static int read_uint64(FILE *f, uint64_t *value_out) {
  unsigned char data[8];
  if (fread(data, 8, 1, f)!=1)
    return 0;
  *value_out = (((((((((((( (data[0]<<8) + data[1])<<8) + data[2])<<8) + data[3])<<8) + data[4])<<8) + data[5])<<8) + data[6])<<8) + data[7];
  return 1;
}

static int write_uint64(FILE *f, uint64_t value) {
  unsigned char data[8];
  data[0] = value>>56;
  data[1] = value>>48;
  data[2] = value>>40;
  data[3] = value>>32;
  data[4] = value>>24;
  data[5] = value>>16;
  data[6] = value>> 8;
  data[7] = value>> 0;
  return fwrite(data, 1, 8, f)==8;
}

static int read_string(FILE *f, char **value_out) {
  unsigned len;
  if (!read_uint32(f, &len))
    return 0;
  *value_out = (char*) malloc(len+1);
  if (!*value_out)
    return 0;
  if (fread(*value_out, len, 1, f)!=1)
    return 0;
  (*value_out)[len] = 0;
  return 1;
}

static int write_string(FILE *f, const char *value) {
  unsigned len = strlen(value);
  if (!write_uint32(f, len))
    return 0;
  if (fwrite(value, len, 1, f)!=1)
    return 0;
  return 1;
}

/***/


unsigned kTest_getCurrentVersion() {
  return KTEST_VERSION;
}


static int kTest_checkHeader(FILE *f) {
  char header[KTEST_MAGIC_SIZE];
  if (fread(header, KTEST_MAGIC_SIZE, 1, f)!=1)
    return 0;
  if (memcmp(header, KTEST_MAGIC, KTEST_MAGIC_SIZE) &&
      memcmp(header, BOUT_MAGIC, KTEST_MAGIC_SIZE))
    return 0;
  return 1;
}

int kTest_isKTestFile(const char *path) {
  FILE *f = fopen(path, "rb");
  int res;

  if (!f)
    return 0;
  res = kTest_checkHeader(f);
  fclose(f);
  
  return res;
}

KTest *kTest_fromFile(const char *path) {
  FILE *f = fopen(path, "rb");
  KTest *res = 0;
  unsigned i, version;

  if (!f) 
    goto error;
  if (!kTest_checkHeader(f)) 
    goto error;

  res = (KTest*) calloc(1, sizeof(*res));
  if (!res) 
    goto error;

  if (!read_uint32(f, &version)) 
    goto error;
  
  if (version > kTest_getCurrentVersion())
    goto error;

  res->version = version;

  if (!read_uint32(f, &res->numArgs)) 
    goto error;
  res->args = (char**) calloc(res->numArgs, sizeof(*res->args));
  if (!res->args) 
    goto error;
  
  for (i=0; i<res->numArgs; i++)
    if (!read_string(f, &res->args[i]))
      goto error;

  if (version >= 2) {
    if (!read_uint32(f, &res->symArgvs)) 
      goto error;
    if (!read_uint32(f, &res->symArgvLen)) 
      goto error;
  }

  if (!read_uint32(f, &res->numObjects))
    goto error;
  res->objects = (KTestObject*) calloc(res->numObjects, sizeof(*res->objects));
  if (!res->objects)
    goto error;
  for (i=0; i<res->numObjects; i++) {
    KTestObject *o = &res->objects[i];
    if (!read_string(f, &o->name))
      goto error;
    if (res->version >= 4) { // Cliver-specific version 4
      if (!read_uint64(f, (uint64_t*)&o->timestamp.tv_sec))
	goto error;
      if (!read_uint64(f, (uint64_t*)&o->timestamp.tv_usec))
	goto error;
    }
    if (!read_uint32(f, &o->numBytes))
      goto error;
    o->bytes = (unsigned char*) malloc(o->numBytes);
    if (o->numBytes && fread(o->bytes, o->numBytes, 1, f)!=1)
      goto error;
  }

  fclose(f);

  return res;
 error:
  if (res) {
    if (res->args) {
      for (i=0; i<res->numArgs; i++)
        if (res->args[i])
          free(res->args[i]);
      free(res->args);
    }
    if (res->objects) {
      for (i=0; i<res->numObjects; i++) {
        KTestObject *bo = &res->objects[i];
        if (bo->name)
          free(bo->name);
        if (bo->bytes)
          free(bo->bytes);
      }
      free(res->objects);
    }
    free(res);
  }

  if (f) fclose(f);

  return 0;
}


int kTest_toFile(KTest *bo, const char *path) {
  FILE *f = fopen(path, "wb");
  unsigned i;

  if (!f) 
    goto error;
  if (fwrite(KTEST_MAGIC, strlen(KTEST_MAGIC), 1, f)!=1)
    goto error;
  if (!write_uint32(f, KTEST_VERSION))
    goto error;
      
  if (!write_uint32(f, bo->numArgs))
    goto error;
  for (i=0; i<bo->numArgs; i++) {
    if (!write_string(f, bo->args[i]))
      goto error;
  }

  if (!write_uint32(f, bo->symArgvs))
    goto error;
  if (!write_uint32(f, bo->symArgvLen))
    goto error;
  
  if (!write_uint32(f, bo->numObjects))
    goto error;
  for (i=0; i<bo->numObjects; i++) {
    KTestObject *o = &bo->objects[i];
    if (!write_string(f, o->name))
      goto error;
    if (!write_uint32(f, o->numBytes))
      goto error;
    if (fwrite(o->bytes, o->numBytes, 1, f)!=1)
      goto error;
  }

  fclose(f);

  return 1;
 error:
  if (f) fclose(f);
  
  return 0;
}

unsigned kTest_numBytes(KTest *bo) {
  unsigned i, res = 0;
  for (i=0; i<bo->numObjects; i++)
    res += bo->objects[i].numBytes;
  return res;
}

void kTest_free(KTest *bo) {
  unsigned i;
  for (i=0; i<bo->numArgs; i++)
    free(bo->args[i]);
  free(bo->args);
  for (i=0; i<bo->numObjects; i++) {
    free(bo->objects[i].name);
    free(bo->objects[i].bytes);
  }
  free(bo->objects);
  free(bo);
}


enum { CLIENT_TO_SERVER=0, SERVER_TO_CLIENT, RNG, PRNG, TIME, STDIN, SELECT,
       MASTER_SECRET };
static char* ktest_object_names[] = {
  "c2s", "s2c", "rng", "prng", "time", "stdin", "select", "master_secret"
};


KTestObjectVector ktov;
static enum kTestMode ktest_mode = KTEST_NONE;
static const char *ktest_output_file = "s_client.ktest";
static const char *ktest_network_file = "s_client.net.ktest";
static int ktest_sockfd = -1; // descriptor of the socket we're capturing


// KTOV = "KTestObjectVector"
static void KTOV_init(KTestObjectVector *ov) {
  memset(ov, 0, sizeof(*ov));
}

static void KTOV_done(KTestObjectVector *ov) {
  if (ov && (ov->objects)) {
    int i;
    for (i = 0; i < ov->size; i++) {
      free(ov->objects[i].name);
      if (ov->objects[i].bytes != NULL) {
	free(ov->objects[i].bytes);
      }
    }
    free(ov->objects);
  }
  memset(ov, 0, sizeof(*ov));
}

static void KTOV_check_mem(KTestObjectVector *ov) {
  if (ov->size + 1 > ov->capacity) {
    size_t new_capacity = (ov->size + 1)*2;
    ov->objects = (KTestObject*) realloc(ov->objects,
					 sizeof(KTestObject) * new_capacity);
    if (!ov->objects) {
      perror("KTOV_check_mem error");
      exit(1);
    }
    ov->capacity = new_capacity;
  }
}

static void timeval2str(char *out, int outlen, const struct timeval *tv) {
  time_t nowtime;
  struct tm *nowtm;
  char tmbuf[64];

  nowtime = tv->tv_sec;
  nowtm = localtime(&nowtime);
  strftime(tmbuf, sizeof tmbuf, "%Y-%m-%d %H:%M:%S", nowtm);
  snprintf(out, outlen, "%s.%06ld", tmbuf, tv->tv_usec);
}

// Print hex and ascii side-by-side
static void KTO_print(FILE *f, const KTestObject *o) {
  unsigned int i, j;
  const unsigned int WIDTH = 16;
  char timebuf[64];

  timeval2str(timebuf, sizeof(timebuf), &o->timestamp);
  fprintf(f, "%s | ", timebuf);
  fprintf(f, "%s [%u]\n", o->name, o->numBytes);
  for (i = 0; WIDTH*i <  o->numBytes; i++) {
    for (j = 0; j < 16 && WIDTH*i+j < o->numBytes; j++) {
      fprintf(f, " %2.2x", o->bytes[WIDTH*i+j]);
    }
    for (; j < 17; j++) {
      fprintf(f, "   ");
    }
    for (j = 0; j < 16 && WIDTH*i+j < o->numBytes; j++) {
      unsigned char c = o->bytes[WIDTH*i+j];
      fprintf(f, "%c", isprint(c)?c:'.');
    }
    fprintf(f, "\n");
  }
  fprintf(f, "\n");
}


// Deep copy of KTestObject
static void KTO_deepcopy(KTestObject *dest, KTestObject *src) {
  dest->name = strdup(src->name);
  dest->timestamp = src->timestamp;
  dest->numBytes = src->numBytes;
  dest->bytes = (unsigned char*)malloc(sizeof(unsigned char)*src->numBytes);
  memcpy(dest->bytes, src->bytes, src->numBytes);
}

static void KTOV_print(FILE *f, const KTestObjectVector *ov) {
  int i;
  fprintf(f, "KTestObjectVector of size %d and capacity %d:\n\n",
	  ov->size, ov->capacity);
  for (i = 0; i < ov->size; i++) {
    fprintf(f, "#%d: ", i);
    KTO_print(f, &ov->objects[i]);
  }
}

KTestObject* KTOV_next_object(KTestObjectVector *ov, const char *name)
{
  if (ov->playback_index >= ov->size) {
    fprintf(stderr, "ERROR: ktest playback %s - no more recorded events", name);
    exit(2);
  }
  KTestObject *o = &ov->objects[ov->playback_index];
  if (strcmp(o->name, name) != 0) {
    fprintf(stderr,
	    "ERROR: ktest playback needed '%s', but recording had '%s'\n",
	    name, o->name);
    exit(2);
  }
  ov->playback_index++;
  return o;
}

void ktest_start(const char *filename, enum kTestMode mode) {
  KTOV_init(&ktov);
  ktest_mode = mode;

  // set ktest output filename and ktest network-only filename
  if (filename != NULL) {
    char *network_file = NULL;
    const char *suffix = ".net.ktest";
    const char *ext = ".ktest";
    int n_ext = strlen(ext);
    int n_f = strlen(filename);
    int n_suf = strlen(suffix);
    ktest_output_file = filename;
    network_file = (char *)malloc(sizeof(char) * (n_f + n_suf + 1));
    strcpy(network_file, filename);
    if (n_f > n_ext && strcmp(&filename[n_f-n_ext], ext) == 0) {
      strcpy(&network_file[n_f-n_ext], suffix);
    } else {
      strcat(network_file, suffix);
    }
    ktest_network_file = network_file;
  }

  // Load capture from file if playback mode
  if (ktest_mode == KTEST_PLAYBACK) {
    KTest *ktest;
    ktest = kTest_fromFile(filename);
    if (!ktest) {
      fprintf(stderr, "Error reading ktest file: %s\n", filename);
      exit(1);
    }
    ktov.size = ktov.capacity = ktest->numObjects;
    printf("Found %d ktov entries \n", ktov.size);
    ktov.objects = (KTestObject*)malloc(sizeof(KTestObject) * ktov.size);
    int i;
    for (i = 0; i < ktov.size; i++) {
      KTO_deepcopy(&ktov.objects[i], &ktest->objects[i]);
    }
    kTest_free(ktest);
  }
}

void ktest_finish() {
  KTest ktest;

  if (ktest_mode == KTEST_NONE) {
    return;
  }

  else if (ktest_mode == KTEST_RECORD) {
    memset(&ktest, 0, sizeof(KTest));
    ktest.numObjects = ktov.size;
    ktest.objects = ktov.objects;

    KTOV_print(stdout, &ktov);

    int result = kTest_toFile(&ktest, ktest_output_file);
    if (!result) {
      perror("ktest_finish error");
      exit(1);
    }
    printf("KTest full capture written to %s.\n", ktest_output_file);

    // Sort network events to the front and write as separate file.
    int i, filtered_i;
    for (i = 0, filtered_i = 0; i < ktest.numObjects; i++) {
      if (strcmp(ktest.objects[i].name, "s2c") == 0 ||
	  strcmp(ktest.objects[i].name, "c2s") == 0) {
	KTestObject temp;
	temp = ktest.objects[filtered_i];
	ktest.objects[filtered_i] = ktest.objects[i];
	ktest.objects[i] = temp;
	filtered_i++;
      }
    }
    ktest.numObjects = filtered_i;

    result = kTest_toFile(&ktest, ktest_network_file);
    if (!result) {
      perror("ktest_finish error");
      exit(1);
    }
    printf("KTest network capture written to %s.\n", ktest_network_file);

    KTOV_done(&ktov);
  }

  else if (ktest_mode == KTEST_PLAYBACK) {
    // TODO: nothing except maybe cleanup?
  }
}

KTestObject * peekNextKTestObject () {
  if (ktov.playback_index >= ktov.size){
    printf("Tried to peek for nonexistent KTestObject \n");
    std::exit(EXIT_FAILURE);
  }else {
    return &(ktov.objects[ktov.playback_index]);
  }
}
