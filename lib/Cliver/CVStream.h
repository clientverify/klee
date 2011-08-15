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
#include <iomanip>
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Instructions.h"
#include "klee/Internal/Module/KInstruction.h"
#include "klee/Internal/Module/InstructionInfoTable.h"

// TODO move to util header
#define foreach BOOST_FOREACH 
#define reverse_foreach BOOST_REVERSE_FOREACH 

#define CV_DEBUG_FILE "debug.txt"
#define CV_WARNING_FILE "warnings.txt"
#define CV_INFO_FILE "info.txt"
#define CV_MESSAGE_FILE "messages.txt"

namespace cliver {

extern std::ostream* cv_warning_stream;
extern std::ostream* cv_message_stream;
extern std::ostream* cv_debug_stream;

// http://www.parashift.com/c++-faq-lite/misc-technical-issues.html#faq-39.6
#define CONCAT_TOKEN_(foo, bar) CONCAT_TOKEN_IMPL_(foo, bar)
#define CONCAT_TOKEN_IMPL_(foo, bar) foo ## bar

#define CVMESSAGE(__x) \
	*cv_message_stream <<"CV: "<< __x << "\n";

#define __CVDEBUG_FILEPOS \
	"CV: DEBUG (" __FILE__ ":"  << __LINE__  << ") "

#define __CVDEBUG_FILE \
  "CV: DEBUG " << std::setw(25) << std::left << "(" __FILE__ ") "

#define __CVDEBUG(__debug_enabled, __x) \
	if (__debug_enabled) { \
	*cv_debug_stream << __CVDEBUG_FILE << __x << "\n"; }

#define __CVDEBUG_S(__debug_enabled, __state_id, __x) \
	if (__debug_enabled) { \
	*cv_debug_stream << __CVDEBUG_FILE << "State: " \
   << std::setw(4) << std::right << __state_id << " - " << __x << "\n"; } 

#define __CVDEBUG_S2(__debug_enabled, __state_id_1, __state_id_2, __x) \
	if (__debug_enabled) { \
	*cv_debug_stream << __CVDEBUG_FILE << "States: (" \
   <<  __state_id_1 << ", " << __state_id_2 << ") " <<__x << "\n"; }

#define CVDEBUG(__x) \
	__CVDEBUG(true, __x)

#define CVDEBUG_S(__state_id, __x) \
	__CVDEBUG_S(true, __state_id, __x)

#define CVDEBUG_S2(__state_id_1, __state_id_2, __x) \
	__CVDEBUG_S2(true, __state_id_1, __state_id_2, __x)

////////////////////////////////////////////////////////////////////////////////

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

void util_inst_string( llvm::Instruction* inst, std::string &rstr);
void util_kinst_string( klee::KInstruction* kinst, std::string &rstr);

////////////////////////////////////////////////////////////////////////////////

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

////////////////////////////////////////////////////////////////////////////////

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

  std::string getOutputFilename(const std::string &filename);
  std::ostream* openOutputFile(const std::string &filename);
	void getOutFiles(std::string path, std::vector<std::string> &results);
	void getFiles(std::string path, std::string suffix,
			std::vector<std::string> &results);

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

////////////////////////////////////////////////////////////////////////////////

inline std::ostream &operator<<(std::ostream &os, 
		const klee::KInstruction &ki) {
	std::string str;
	llvm::raw_string_ostream ros(str);
	ros << ki.info->id << ":" << *ki.inst;
	//str.erase(std::remove(str.begin(), str.end(), '\n'), str.end());
	return os << ros.str();
}


} // end namespace cliver
#endif // CLIVER_STREAM_H

