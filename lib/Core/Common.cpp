//===-- Common.cpp --------------------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "Common.h"

#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <assert.h>
#include <string.h>

#include <set>
#include <ostream>

using namespace klee;

FILE* klee::klee_warning_file = NULL;
FILE* klee::klee_message_file = NULL;

std::ostream* klee::klee_warning_stream = NULL;
std::ostream* klee::klee_message_stream = NULL;

static void klee_vfmessage(FILE *fp, const char *pfx, const char *msg, 
                           va_list ap) {
  if (!fp)
    return;

  fprintf(fp, "KLEE: ");
  if (pfx) fprintf(fp, "%s: ", pfx);
  vfprintf(fp, msg, ap);
  fprintf(fp, "\n");
  fflush(fp);
}

static void klee_vomessage(std::ostream* os, const char *pfx, const char *msg, 
                           va_list ap) {
  if (!os)
    return;

	*os << "KLEE: ";
	if (pfx)
		*os << pfx << ": ";

	char buf[1024];
	vsnprintf(buf, sizeof(buf), msg, ap);
	*os << buf << std::endl;
}

/* Prints a message/warning.
   
   If pfx is NULL, this is a regular message, and it's sent to
   klee_message_file (messages.txt).  Otherwise, it is sent to 
   klee_warning_file (warnings.txt).

   Iff onlyToFile is false, the message is also printed on stderr.
*/
static void klee_vmessage(const char *pfx, bool onlyToFile, const char *msg, 
                          va_list ap) {
  
	//klee_vfmessage(pfx ? klee_message_file : klee_warning_file, pfx, msg, ap);
	klee_vomessage(pfx ? klee_warning_stream : klee_message_stream, pfx, msg, ap);
}


void klee::klee_message(const char *msg, ...) {
  va_list ap;
  va_start(ap, msg);
  klee_vmessage(NULL, false, msg, ap);
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
  static std::set< std::pair<const void*, const char*> > keys;
  std::pair<const void*, const char*> key;


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
    klee_vmessage("WARNING", false, msg, ap);
    va_end(ap);
  }
}
