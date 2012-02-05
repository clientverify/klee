//===-- CVStream.cpp --------------------------------------------*- C++ -*-===//
//
// <insert license>
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//

#include "CVCommon.h"
#include "cliver/CVStream.h"
#include <signal.h>
#include <dirent.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <errno.h>

#include <iostream>
#include <sstream>
#include <fstream>
#include <list>

#include "llvm/System/Path.h"
#include "llvm/Support/ErrorHandling.h"

#include "klee/Interpreter.h"

extern klee::Interpreter *g_interpreter;

namespace {
llvm::cl::opt<std::string>
OutputDir("output-dir", 
  llvm::cl::desc("Directory to write results in (defaults to cliver-out-N)"),
  llvm::cl::init(""));

llvm::cl::opt<bool>
NoOutput("no-output", 
  llvm::cl::desc("Don't generate output files"),
  llvm::cl::init(false));

llvm::cl::opt<std::string>
OutputDirParent("output-dir-parent", 
  llvm::cl::desc("Directory within which to create cliver-out-N"),
  llvm::cl::init("."));

llvm::cl::opt<bool>
DebugStderr("debug-stderr", 
  llvm::cl::desc("Print debug statements onto stderr (also to debug.txt)"),
  llvm::cl::init(false));

llvm::cl::opt<bool>
UseTeeBuf("use-tee-buf",
  llvm::cl::desc("Output to stdout, stderr and files"),
  llvm::cl::init(true));

llvm::cl::opt<bool>
CVStreamPrintInstructions("cvstream-print-inst",
  llvm::cl::desc("Print instructions in CVStream"),
  llvm::cl::init(false));
}

////////////////////////////////////////////////////////////////////////////////

namespace cliver {

std::ostream* cv_warning_stream = NULL;
std::ostream* cv_message_stream = NULL;
std::ostream* cv_debug_stream   = NULL;

////////////////////////////////////////////////////////////////////////////////

class teebuf: public std::streambuf {
 public:
  teebuf() {}
  teebuf(std::streambuf* sb1, std::streambuf* sb2) {
    add(sb1); add(sb2);
  }
  void add(std::streambuf* sb) { 
    bufs_.push_back(sb);
  }
  virtual int overflow(int c) {
    if (c == EOF) return !EOF;
    int res = c;
    foreach (std::streambuf* buf, bufs_) {
      res = (buf->sputc(c) == EOF) ? EOF : res;
    }
    return res;
  }
  virtual int sync() {
    int res = 0;
    foreach (std::streambuf* buf, bufs_) {
      res = (buf->pubsync() == 0) ? res : -1;
    }
    return res;
  }   
  virtual std::streamsize xsputn(const char* s, std::streamsize n) {
    std::streamsize res = n;
    foreach(std::streambuf* buf, bufs_) {
      std::streamsize ssize = buf->sputn(s, n);
      res = ssize < res ? ssize : res;
    }
    return res;
  }
 private:
  std::list<std::streambuf*> bufs_;
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

static void cv_vomessage(std::ostream* os, const char *pfx, const char *msg, 
    va_list ap) {
  if (!os)
    return;

  *os << "CV: ";
  if (pfx)
    *os << pfx << ": ";

  char buf[1024];
  vsnprintf(buf, sizeof(buf), msg, ap);
  *os << buf << std::endl;
}

void cv_message(const char *msg, ...) {
  va_list ap;
  va_start(ap, msg);
  cv_vomessage(cv_message_stream, NULL, msg, ap);
  va_end(ap);
}

void cv_warning(const char *msg, ...) {
  va_list ap;
  va_start(ap, msg);
  cv_vomessage(cv_warning_stream, "WARNING", msg, ap);
  va_end(ap);
}

void cv_debug(const char *msg, ...) {
  va_list ap;
  va_start(ap, msg);
  cv_vomessage(cv_debug_stream, "DEBUG", msg, ap);
  va_end(ap);
}

void cv_error(const char *msg, ...) {
  va_list ap;
  va_start(ap, msg);
  cv_vomessage(cv_warning_stream, "ERROR", msg, ap);
  va_end(ap);
  exit(1);
}

////////////////////////////////////////////////////////////////////////////////

CVStream::CVStream()
  : output_directory_(OutputDir),
  initialized_(false) {
}

CVStream::~CVStream() {
  if (initialized_) {
    if (!NoOutput) {
      delete info_file_stream_;
      delete debug_file_stream_;
      delete message_file_stream_;
      delete warning_file_stream_;
    }
    delete info_stream_;
    delete debug_stream_;
    delete message_stream_;
    delete warning_stream_;
  }
}

std::string CVStream::getOutputFilename(const std::string &filename) {
  llvm::sys::Path filepath(output_directory_);
  filepath.appendComponent(filename);
  return filepath.str();
}

std::ostream *CVStream::openOutputFile(const std::string &filename) {
  if (NoOutput) {
    teestream* null_teestream = new teestream();
    std::cerr << "output files disabled: \"" << filename 
      << "\"\n";
    return static_cast<std::ostream*>(null_teestream);
  }

  std::ios::openmode io_mode 
    = std::ios::out | std::ios::trunc | std::ios::binary;
  std::ostream *f;
  std::string path = getOutputFilename(filename);
  f = new std::ofstream(path.c_str(), io_mode);
  if (!f) {
    if (initialized_)
      klee::klee_error("error opening file \"%s\" (out of memory)", 
          filename.c_str());
    else
      std::cerr << "error opening file \""<<filename<<"\" (out of memory)\n";
  } else if (!f->good()) {
    if (initialized_)
      klee::klee_error("error opening file \"%s\" ", filename.c_str());
    else
      std::cerr << "error opening file \""<<filename<<"\"\n";
    delete f;
    f = NULL;
  }

  return f;
}

void CVStream::initOutputDirectory() {

  if (output_directory_.empty() || output_directory_ == "") {
    for (int i = 0; ; i++) {
      std::ostringstream dir_name;
      dir_name << "cliver-out-" << i;

      llvm::sys::Path dir_path(OutputDirParent);
      dir_path.appendComponent(dir_name.str());

      if (!dir_path.exists()) {
        output_directory_ = dir_path.str();
        break;
      }
    }    

    llvm::sys::Path cliver_last(OutputDirParent);
    cliver_last.appendComponent("cliver-last");

    if ((unlink(cliver_last.c_str()) < 0) && (errno != ENOENT)) {
      perror("Cannot unlink cliver-last");
      exit(1);
    }

    if (symlink(output_directory_.c_str(), cliver_last.c_str()) < 0) {
      perror("Cannot make symlink");
      exit(1);
    }
  }

  if (mkdir(output_directory_.c_str(), 0775) < 0) {
    std::cerr << "CV: ERROR: Unable to make output directory: \"" 
      << output_directory_ 
      << "\", refusing to overwrite.\n";
    exit(1);
  }

}

void CVStream::getOutFiles(std::string path, 
		std::vector<std::string> &results) {
	getFiles(path, ".ktest", results);
}

void CVStream::getFiles(std::string path, std::string suffix, 
		std::vector<std::string> &results) {
  llvm::sys::Path p(path);
  std::set<llvm::sys::Path> contents;
  std::string error;
  if (p.getDirectoryContents(contents, &error)) {
    std::cerr << "ERROR: unable to read output directory: " << path 
               << ": " << error << "\n";
    exit(1);
  }
  for (std::set<llvm::sys::Path>::iterator it = contents.begin(),
         ie = contents.end(); it != ie; ++it) {
    std::string f = it->str();
    if (f.substr(f.size()-suffix.size(), f.size()) == suffix) {
      results.push_back(f);
    }
  }
}

void CVStream::init() {
  if (!NoOutput)
    initOutputDirectory();

  using std::ios_base;
  ios_base::sync_with_stdio(true);
  std::cout.setf(ios_base::unitbuf);
  std::cerr.setf(ios_base::unitbuf);

  if (UseTeeBuf) {
    if (!NoOutput) {
      info_file_stream_    = openOutputFile(CV_INFO_FILE);
      warning_file_stream_ = openOutputFile(CV_WARNING_FILE);
      message_file_stream_ = openOutputFile(CV_MESSAGE_FILE);
      debug_file_stream_   = openOutputFile(CV_DEBUG_FILE);

      // Flush stream with every write operation
      info_file_stream_->setf(ios_base::unitbuf);
      debug_file_stream_->setf(ios_base::unitbuf);
      warning_file_stream_->setf(ios_base::unitbuf);
      message_file_stream_->setf(ios_base::unitbuf);
    }

    teestream* info_teestream = new teestream();
    teestream* message_teestream = new teestream();
    teestream* warning_teestream = new teestream();
    teestream* debug_teestream = new teestream();

    info_teestream->setf(ios_base::unitbuf);
    debug_teestream->setf(ios_base::unitbuf);
    warning_teestream->setf(ios_base::unitbuf);
    message_teestream->setf(ios_base::unitbuf);
 
    info_teestream->add(std::cout);
    message_teestream->add(std::cout);
    warning_teestream->add(std::cout);
    if (DebugStderr) 
      debug_teestream->add(std::cerr);

    if (!NoOutput) {
      info_teestream->add(*info_file_stream_);
      info_teestream->add(*debug_file_stream_);

      message_teestream->add(*message_file_stream_);
      message_teestream->add(*debug_file_stream_);

      warning_teestream->add(*warning_file_stream_);
      warning_teestream->add(*debug_file_stream_);

      debug_teestream->add(*debug_file_stream_);
    }

    info_stream_    = static_cast<std::ostream*>(info_teestream);
    message_stream_ = static_cast<std::ostream*>(message_teestream);
    warning_stream_ = static_cast<std::ostream*>(warning_teestream);
    debug_stream_   = static_cast<std::ostream*>(debug_teestream);

  } else {

    info_stream_    = &(std::cout);
    message_stream_ = &(std::cout);
    warning_stream_ = &(std::cout);
    if (DebugStderr) 
      debug_stream_   = &(std::cerr);
    else
      debug_stream_   = &(std::cout);
  }

  klee::klee_warning_stream = warning_stream_;
  klee::klee_message_stream = message_stream_;

  cv_warning_stream = warning_stream_;
  cv_message_stream = message_stream_;
  cv_debug_stream   = debug_stream_;

  initialized_ = true;
}

////////////////////////////////////////////////////////////////////////////////

} // end namespace cliver

