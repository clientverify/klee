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

#include <stdio.h>

#include <iomanip>
#include <ostream>

#include "../lib/Core/Common.h"

#define CV_DEBUG_FILE "debug.txt"
#define CV_WARNING_FILE "warnings.txt"
#define CV_INFO_FILE "info.txt"
#define CV_MESSAGE_FILE "messages.txt"

namespace cliver {

extern std::ostream* cv_warning_stream;
extern std::ostream* cv_message_stream;
extern std::ostream* cv_debug_stream;

#define CVMESSAGE(__x) \
	*cv_message_stream <<"CV: "<< __x << "\n";

#ifndef NDEBUG

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

#else

#define __CVDEBUG_FILEPOS
#define __CVDEBUG_FILE
#define __CVDEBUG(__debug_enabled, __x)
#define __CVDEBUG_S(__debug_enabled, __state_id, __x)
#define __CVDEBUG_S2(__debug_enabled, __state_id_1, __state_id_2, __x)
#define CVDEBUG(__x)
#define CVDEBUG_S(__state_id, __x)
#define CVDEBUG_S2(__state_id_1, __state_id_2, __x)

#endif //NDEBUG
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

  std::string getBasename(const std::string &filename);
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

} // end namespace cliver
#endif // CLIVER_STREAM_H

