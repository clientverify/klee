//===-- CVStream.cpp --------------------------------------------*- C++ -*-===//
//
// <insert license>
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//

#include "cliver/CVStream.h"
#include "CVCommon.h"

#include "../lib/Core/Common.h"

#include "klee/Interpreter.h"

#include "llvm/ADT/SmallString.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/raw_os_ostream.h"

#include "klee/Config/Version.h"

#include <list>
#include <sstream>

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include <time.h>

namespace cliver {
llvm::cl::opt<std::string>
OutputDirParent("output-dir-parent", 
  llvm::cl::desc("Directory within which to create cliver-out-N"),
  llvm::cl::init("."));

llvm::cl::opt<bool>
DebugStderr("debug-stderr", 
  llvm::cl::desc("Print debug statements onto stderr (also to debug.txt) (Default=on)"),
  llvm::cl::init(true));

llvm::cl::opt<bool>
UseTeeBuf("use-tee-buf",
  llvm::cl::desc("Output to stdout, stderr and files"),
  llvm::cl::init(true));

llvm::cl::opt<bool>
CVStreamPrintInstructions("cvstream-print-inst",
  llvm::cl::desc("Print instructions in CVStream"),
  llvm::cl::init(false));
}

llvm::cl::opt<bool>
MinimalOutput("minimal-output",
  llvm::cl::desc("Minimal output to only one file, not piped to stdout/stderr"),
  llvm::cl::init(false));

////////////////////////////////////////////////////////////////////////////////

namespace cliver {

std::ostream* cv_warning_stream = NULL;
std::ostream* cv_message_stream = NULL;
std::ostream* cv_debug_stream   = NULL;

klee::Mutex*  cv_stream_lock = NULL;

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

  char timebuff[20];
  time_t rawtime;
  struct tm * timeinfo;
  time(&rawtime);
  timeinfo = localtime(&rawtime);
  strftime(timebuff, sizeof(timebuff), "%Y-%m-%d %H:%M:%S", timeinfo);

  *os << timebuff << " | ";
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

CVStream::CVStream(bool no_output, std::string &output_dir)
  : initialized_(false),
    no_output_(no_output),
    output_dir_(output_dir),
    info_file_stream_(NULL),
    debug_file_stream_(NULL),
    message_file_stream_(NULL),
    warning_file_stream_(NULL) {
  cv_stream_lock = new klee::Mutex();
}

CVStream::~CVStream() {
  if (cv_stream_lock)
    delete cv_stream_lock;

  if (info_file_stream_) {
    delete info_file_stream_;
    info_file_stream_ = NULL;
  }
  if (debug_file_stream_) {
    delete debug_file_stream_;
    debug_file_stream_ = NULL;
  }
  if (message_file_stream_) {
    delete message_file_stream_;
    message_file_stream_ = NULL;
  }
  if (warning_file_stream_) {
    delete warning_file_stream_;
    warning_file_stream_ = NULL;
  }
}

std::string CVStream::getBasename(const std::string &filename) {
  return llvm::sys::path::filename(filename).str();
}

std::string CVStream::appendComponent(const std::string &filename,
                                      const std::string &append) {
  llvm::SmallString<128> filepath(filename);
  llvm::sys::path::append(filepath,append);
  return filepath.str();
}

std::string CVStream::getOutputFilename(const std::string &filename) {
  llvm::SmallString<128> path(output_dir_);
  llvm::sys::path::append(path,filename);
  return path.str();
}

std::ostream *CVStream::openOutputFile(const std::string &filename) {
  std::string sub_dir("");
  return openOutputFileInSubDirectory(filename, sub_dir);
}

llvm::raw_fd_ostream *CVStream::openOutputFileLLVM(const std::string &filename) {
  llvm::raw_fd_ostream *f;
  std::string Error;
  std::string path = getOutputFilename(filename);
#if LLVM_VERSION_CODE >= LLVM_VERSION(3,0)
  f = new llvm::raw_fd_ostream(path.c_str(), Error, llvm::sys::fs::F_Binary);
#else
  f = new llvm::raw_fd_ostream(path.c_str(), Error, llvm::raw_fd_ostream::F_Binary);
#endif
  if (!Error.empty()) {
    klee::klee_error("error opening file \"%s\".  KLEE may have run out of file "
               "descriptors: try to increase the maximum number of open file "
               "descriptors by using ulimit (%s).",
               filename.c_str(), Error.c_str());
    delete f;
    f = NULL;
  }
  return f;
}

std::ostream *CVStream::openOutputFileInSubDirectory(
    const std::string &filename, const std::string &sub_directory) {
  if (no_output_) {
    teestream* null_teestream = new teestream();
    std::cerr << "output files disabled: \"" << filename 
      << "\"\n";
    return static_cast<std::ostream*>(null_teestream);
  }

  std::ios::openmode io_mode 
    = std::ios::out | std::ios::trunc | std::ios::binary;
  std::ostream *f;
  std::string path;

  if (sub_directory != "") {
    path = getOutputFilename(sub_directory);
    if (mkdir(path.c_str(), 0775) < 0) {
      if (errno != EEXIST) {
        std::cerr << "CV: ERROR: Unable to make directory: \"" 
          << path
          << "\", refusing to overwrite.\n";
        exit(1);
      }
    }
    path = appendComponent(path, filename);
  } else {
    path = getOutputFilename(filename);
  }

  f = new std::ofstream(path.c_str(), io_mode);
  if (!f) {
    if (initialized_)
      klee::klee_error("error opening file \"%s\" (out of memory)", 
          path.c_str());
    else
      std::cerr << "error opening file \""<<path<<"\" (out of memory)\n";
  } else if (!f->good()) {
    if (initialized_)
      klee::klee_error("error opening file \"%s\" ", path.c_str());
    else
      std::cerr << "error opening file \""<<path<<"\"\n";
    delete f;
    f = NULL;
  }
  return f;
}

void CVStream::initOutputDirectory() {

  if (output_dir_.empty() || output_dir_ == "") {
    int i = 0;
    for (; i<INT_MAX; i++) {

      llvm::SmallString<128> dir_path(OutputDirParent);
      llvm::sys::path::append(dir_path, "cliver-out-");
      llvm::raw_svector_ostream ds(dir_path); ds << i; ds.flush();

      // create directory and try to link 
      if (mkdir(dir_path.c_str(), 0775) == 0) {
        output_dir_ = std::string(dir_path.c_str());

        llvm::SmallString<128> cliver_last(OutputDirParent);
        llvm::sys::path::append(cliver_last, "cliver-last");

        if (((unlink(cliver_last.c_str()) < 0) && (errno != ENOENT)) ||
            symlink(output_dir_.c_str(), cliver_last.c_str()) < 0) {
          klee::klee_warning("cannot create cliver-last symlink: %s", 
                             strerror(errno));
        }
        break;
      }

      // otherwise try again or exit on error
      if (errno != EEXIST)
        klee::klee_error("cannot create \"%s\": %s", 
                        output_dir_.c_str(), strerror(errno));
    }
    if ( i == INT_MAX ) {
      klee::klee_error("cannot create output directory: index out of range");
    }
  } else {
    // Create output directory
    if (mkdir(output_dir_.c_str(), 0775) < 0) {
      klee::klee_error("cannot create \"%s\": %s", output_dir_.c_str(), 
                      strerror(errno));
    }
  }
}

static int cp(const char *to, const char *from)
{
  int fd_to, fd_from;
  char buf[4096];
  ssize_t nread;
  int saved_errno;

  fd_from = open(from, O_RDONLY);
  if (fd_from < 0)
      return -1;

  fd_to = open(to, O_WRONLY | O_CREAT | O_EXCL, 0666);
  if (fd_to < 0)
      goto out_error;

  while (nread = read(fd_from, buf, sizeof buf), nread > 0) {
    char *out_ptr = buf;
    ssize_t nwritten;

    do {
      nwritten = write(fd_to, out_ptr, nread);

      if (nwritten >= 0) {
        nread -= nwritten;
        out_ptr += nwritten;
      }
      else if (errno != EINTR) {
        goto out_error;
      }
    } while (nread > 0);
  }

  if (nread == 0) {
    if (close(fd_to) < 0) {
      fd_to = -1;
      goto out_error;
    }
    close(fd_from);

    /* Success! */
    return 0;
  }

out_error:
  saved_errno = errno;

  close(fd_from);
  if (fd_to >= 0)
      close(fd_to);

  errno = saved_errno;
  return -1;
}

void CVStream::copyFileToOutputDirectory(const std::string &src_path,
                                         const std::string &dst_name) {
  if (no_output_)
    return;

  assert(!output_dir_.empty() && output_dir_ != "");

  std::string dst_path = appendComponent(output_dir_, dst_name);

  if (cp(dst_path.c_str(), src_path.c_str())) {
    std::cerr << "ERROR: unable to copy file " << src_path << "\n";
    exit(1);
  }
}

void CVStream::getOutFiles(std::string path, 
		std::vector<std::string> &results) {
	getFiles(path, ".ktest", results);
}

void CVStream::getFiles(std::string path, std::string suffix, 
		std::vector<std::string> &results) {
#if LLVM_VERSION_CODE >= LLVM_VERSION(3, 3)
  llvm::error_code ec;
  for (llvm::sys::fs::directory_iterator di(path, ec), de;
       !ec && di != de; di = di.increment(ec)) {
    if (suffix == llvm::sys::path::extension(di->path())) {
      results.push_back(di->path());
    }
  }
#else
  llvm::sys::Path p(path);
  std::set<llvm::sys::Path> contents;
  std::string error;
  if (p.getDirectoryContents(contents, &error)) {
    std::cerr << "ERROR: For getFilesRecursive( " 
			<< path << ", " << suffix << ") : " << error << "\n";
    exit(1);
  }
  for (std::set<llvm::sys::Path>::iterator it = contents.begin(),
         ie = contents.end(); it != ie; ++it) {
    std::string f = it->str();
    if (it->isDirectory()) {
      getFilesRecursive(f, suffix, results);
    } else {
      if (f.substr(f.size()-suffix.size(), f.size()) == suffix) {
        results.push_back(f);
      }
    }
  }
#endif
}

void CVStream::getFilesRecursive(std::string path, 
                                 std::string suffix, 
                                 std::vector<std::string> &results) {
#if LLVM_VERSION_CODE >= LLVM_VERSION(3, 3)
  llvm::error_code ec;
  for (llvm::sys::fs::recursive_directory_iterator di(path, ec), de;
       !ec && di != de; di = di.increment(ec)) {
    if (suffix == llvm::sys::path::extension(di->path())) {
      results.push_back(di->path());
    }
  }
#else
  llvm::sys::Path p(path);
  std::set<llvm::sys::Path> contents;
  std::string error;
  if (p.getDirectoryContents(contents, &error)) {
    std::cerr << "ERROR: For getFiles( " 
			<< path << ", " << suffix << ") : " << error << "\n";
    exit(1);
  }
  for (std::set<llvm::sys::Path>::iterator it = contents.begin(),
         ie = contents.end(); it != ie; ++it) {
    std::string f = it->str();
    if (f.substr(f.size()-suffix.size(), f.size()) == suffix) {
      results.push_back(f);
    }
  }
#endif
}

void CVStream::init() {
  if (!no_output_)
    initOutputDirectory();

  using std::ios_base;
  ios_base::sync_with_stdio(true);
  std::cout.setf(ios_base::unitbuf);
  std::cerr.setf(ios_base::unitbuf);

  if (MinimalOutput) {
    debug_file_stream_   = openOutputFile(CV_DEBUG_FILE);

    info_stream_    = debug_file_stream_;
    message_stream_ = debug_file_stream_;
    warning_stream_ = debug_file_stream_;
    debug_stream_   = debug_file_stream_;

  } else if (UseTeeBuf) {
    if (!no_output_) {
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

    if (!no_output_) {
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

  raw_info_stream_ = new llvm::raw_os_ostream(*info_stream_);
  initialized_ = true;
}

////////////////////////////////////////////////////////////////////////////////

} // end namespace cliver

