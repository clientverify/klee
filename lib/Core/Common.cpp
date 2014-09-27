//===-- Common.cpp --------------------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "Common.h"
#include "klee/util/Mutex.h"

#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <assert.h>
#include <string.h>
#include <time.h>

#include <set>
#include <vector>
#include <ostream>

using namespace klee;

FILE* klee::klee_warning_file = NULL;
FILE* klee::klee_message_file = NULL;

std::ostream* klee::klee_warning_stream = NULL;
std::ostream* klee::klee_message_stream = NULL;

static void klee_vfmessage(FILE *fp, const char *pfx, const char *msg, 
                           va_list ap) {
  if (!fp) {
    return;
  }

  char timebuff[20];
  time_t rawtime;
  struct tm * timeinfo;
  time(&rawtime);
  timeinfo = localtime(&rawtime);
  strftime(timebuff, sizeof(timebuff), "%Y-%m-%d %H:%M:%S", timeinfo);

  fprintf(fp, "%s | KLEE: ", timebuff);
  if (pfx) fprintf(fp, "%s: ", pfx);
  vfprintf(fp, msg, ap);
  fprintf(fp, "\n");
  fflush(fp);
}

static void klee_vomessage_write(std::ostream* os, const char *pfx, const char* buf) {
	char timebuff[20];
	time_t rawtime;
	struct tm * timeinfo;
	time(&rawtime);
	timeinfo = localtime(&rawtime);
	strftime(timebuff, sizeof(timebuff), "%Y-%m-%d %H:%M:%S", timeinfo);

        *os << timebuff << " | ";
	*os << "KLEE: ";
	if (pfx)
		*os << pfx << ": ";
	*os << buf << std::endl;
}

static void klee_vomessage(std::ostream* os, const char *pfx, const char *msg, 
                           va_list ap) {
  if (!os) {
    return;
  }

  va_list ap_copy;
  char buf[1024];

  // write to buffer
  va_copy(ap_copy, ap);
  int res = vsnprintf(buf, sizeof(buf), msg, ap_copy);
  va_end(ap_copy);

  if (res >= 0 && res < (int)sizeof(buf)) {
    klee_vomessage_write(os, pfx, buf);
    return;
  }

  // If 1024 buf wasn't big enough
  int buf_size = 1024;
  while (true) {
    buf_size *= 2;
    std::vector<char> heapbuf(buf_size);

    va_copy(ap_copy, ap);
    res = vsnprintf(&heapbuf[0], buf_size, msg, ap_copy);
    va_end(ap_copy);

    if (res >= 0 && res < buf_size) {
      klee_vomessage_write(os, pfx, &heapbuf[0]);
      return;
    }

    if (buf_size >= (1024 * 1024)) {
      const char* error_too_large = "ERROR print request too large";
      klee_vomessage_write(os, pfx, error_too_large);
      return;
    }
  }
}

/* Prints a message/warning.
   
   If pfx is NULL, this is a regular message, and it's sent to
   klee_message_file (messages.txt).  Otherwise, it is sent to 
   klee_warning_file (warnings.txt).

   Iff onlyToFile is false, the message is also printed on stderr.
   FIXME: onlyToFile is ignored
*/
static void klee_vmessage(const char *pfx, bool onlyToFile, const char *msg, 
                          va_list ap) {
  if (klee_warning_stream && klee_message_stream) {
    klee_vomessage(pfx ? klee_warning_stream : klee_message_stream, pfx, msg, ap);
  } else {
    if (!onlyToFile) {
      va_list ap2;
      va_copy(ap2, ap);
      klee_vfmessage(stderr, pfx, msg, ap2);
      va_end(ap2);
    }
    klee_vfmessage(pfx ? klee_warning_file : klee_message_file, pfx, msg, ap);
  }
}

void klee::klee_message(const char *msg, ...) {
  va_list ap;
  va_start(ap, msg);
  klee_vmessage(NULL, false, msg, ap);
  va_end(ap);
}

/* Message to be written only to file */
void klee::klee_message_to_file(const char *msg, ...) {
  va_list ap;
  va_start(ap, msg);
  klee_vmessage(NULL, true, msg, ap);
  va_end(ap);
}

void klee::klee_error(const char *msg, ...) {
  va_list ap;
  va_start(ap, msg);
  klee_vmessage("ERROR", false, msg, ap);
  va_end(ap);
  exit(1);
}

void klee::klee_warning(const char *msg, ...) {
  va_list ap;
  va_start(ap, msg);
  klee_vmessage("WARNING", false, msg, ap);
  va_end(ap);
}

/* Prints a warning once per message. */
void klee::klee_warning_once(const void *id, const char *msg, ...) {
  static Mutex lock;
  static std::set< std::pair<const void*, const char*> > keys;
  std::pair<const void*, const char*> key;

  LockGuard guard(lock);

  /* "calling external" messages contain the actual arguments with
     which we called the external function, so we need to ignore them
     when computing the key. */
  if (strncmp(msg, "calling external", strlen("calling external")) != 0)
    key = std::make_pair(id, msg);
  else key = std::make_pair(id, "calling external");
  
  if (!keys.count(key)) {
    keys.insert(key);
    
    va_list ap;
    va_start(ap, msg);
    klee_vmessage("WARNING ONCE", false, msg, ap);
    va_end(ap);
  }
}
