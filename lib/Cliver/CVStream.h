//===-- CVStream.h ----------------------------------------------*- C++ -*-===//
//
// <insert license>
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//
#ifndef CLIVER_STREAM_H
#define CLIVER_STREAM_H

#include <boost/foreach.hpp>
#include <deque>
#include <dirent.h>
#include <errno.h>
#include <iostream>
#include <ostream>
#include <set>
#include <sstream>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include "llvm/System/Path.h"
#include "../lib/Core/Common.h"
#include <fstream>
#include <map>
#include <string>
#include <vector>

// TODO move to util header
#define foreach BOOST_FOREACH 

#define CV_DEBUG_FILE "debug.txt"
#define CV_WARNING_FILE "warnings.txt"
#define CV_INFO_FILE "info.txt"
#define CV_MESSAGE_FILE "messages.txt"

namespace cliver {

extern std::ostream* cv_warning_stream;
extern std::ostream* cv_message_stream;
extern std::ostream* cv_debug_stream;

/// CV versions of the KLEE error and warning functions
/// Print "CV: ERROR" followed by the msg in printf format to debug stream
/// and then exit with an error
void cv_error(const char *msg, ...)
  __attribute__ ((format (printf, 1, 2), noreturn));

/// Print "CV: DEBUG" followed by the msg in printf format to debug stream
void cv_debug(const char *msg, ...)
  __attribute__ ((format (printf, 1, 2)));

/// Print "CV: " followed by the msg in printf format and to message stream
void cv_message(const char *msg, ...)
  __attribute__ ((format (printf, 1, 2)));

/// Print "CV: WARNING" followed by the msg in printf format to warning stream
void cv_warning(const char *msg, ...)
  __attribute__ ((format (printf, 1, 2)));

class teebuf: public std::streambuf {
 public:
  teebuf() {}
  teebuf(std::streambuf* sb1, std::streambuf* sb2) {
    bufs_.insert(sb1);
    bufs_.insert(sb2);
  }
  void add(std::streambuf* sb) { 
    bufs_.insert(sb);
  }
  virtual int overflow(int c) {
    foreach(std::streambuf* buf, bufs_)
      buf->sputc(c);
    return 1;
  }
  virtual int sync() {
      int r = 0;
      foreach(std::streambuf* buf, bufs_)
          r = buf->pubsync();
    return r;
  }   
  virtual std::streamsize xsputn(const char* s, std::streamsize n) {
    foreach(std::streambuf* buf, bufs_)
      buf->sputn(s, n);
    return n;
  }
 private:
  std::set<std::streambuf*> bufs_;
};

class teestream : public std::ostream {
 public:
  teestream() : std::ostream(&tbuf) {}
  teestream(std::ostream &os1, std::ostream &os2) 
    : std::ostream(&tbuf), tbuf(os1.rdbuf(), os2.rdbuf()) {}
  void add(std::ostream &os) { tbuf.add(os.rdbuf()); }
 private:
  teebuf tbuf;
};

class CVStream {
 public:
  CVStream();
  ~CVStream();

  void init();
  void initOutputDirectory();

  inline std::ostream& info_stream() { return *info_stream_; }
  inline std::ostream& debug_stream() { return *debug_stream_; }
  inline std::ostream& message_stream() { return *message_stream_; }
  inline std::ostream& warning_stream() { return *warning_stream_; }

  std::string   getOutputFilename(const std::string &filename);
  std::ostream* openOutputFile(const std::string &filename);

 private:
  std::string output_directory_;
  bool initialized_;
  std::ostream* info_stream_;
  std::ostream* debug_stream_;
  std::ostream* message_stream_;
  std::ostream* warning_stream_;

  std::ostream* info_file_stream_;
  std::ostream* warning_file_stream_;
  std::ostream* message_file_stream_;
  std::ostream* debug_file_stream_;
};

} // end namespace cliver
#endif // CLIVER_STREAM_H

