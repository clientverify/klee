//===-- ClientVerifier.cpp---------------------------------------*- C++ -*-===//
//
// <insert license>
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//

#include "llvm/Support/CommandLine.h"
#include "ClientVerifier.h"
#include "NetworkManager.h"
#include "../lib/Core/SpecialFunctionHandler.h"
#include "CVExecutor.h"
#include "TestHelper.h"

#include <string>
#include <boost/foreach.hpp>
#define foreach BOOST_FOREACH 

namespace {
llvm::cl::list<std::string>
SocketLogFile("socket-log",
		llvm::cl::ZeroOrMore,
		llvm::cl::ValueRequired,
		llvm::cl::desc("Specify a socket log file"),
		llvm::cl::value_desc("ktest file"));

// TODO support reading multiple socket log files
//llvm::cl::list<std::string>
//SocketLogDir("socket-log-dir",
//		llvm::cl::desc("Specify a directory to replay .ktest files from"),
//		llvm::cl::value_desc("output directory"));
}

namespace cliver {

int CVContext::next_id_ = 0;

CVContext::CVContext() : context_id_(increment_id()) {}

ClientVerifier::ClientVerifier() 
  : cvstream_(new CVStream()) {
  cvstream_->init();
}

ClientVerifier::~ClientVerifier() {
	delete cvstream_;
}	

void ClientVerifier::prepare_to_run(CVExecutor *executor) {
	// init testing helper handlers
	executor->add_external_handler(
			"cliver_test_extract_pointers", ExternalHandler_test_extract_pointers, false);

	executor->add_external_handler(
			"cliver_socket_shutdown", ExternalHandler_socket_shutdown);
	// Add socket external handlers
	executor->add_external_handler(
			"cliver_socket_write", ExternalHandler_socket_write);
	executor->add_external_handler(
			"cliver_socket_read", ExternalHandler_socket_read);
	executor->add_external_handler(
			"cliver_socket_create", ExternalHandler_socket_create);

	// Load socket log files
	if (SocketLogFile.size() > 0) {
		if (load_socket_logs() == 0) {
			cv_error("Error loading socket log files, exiting now.");
		}
	}
}

int ClientVerifier::load_socket_logs() {
	std::vector<std::string> socket_log_names = SocketLogFile;

	foreach (std::string filename, socket_log_names) {
		KTest *ktest = kTest_fromFile(filename.c_str());
		if (ktest) {
			socket_logs_.push_back(ktest);
			cv_message("Opened socket log \"%s\" with %d objects",
					filename.c_str(), ktest->numObjects);
		} else {
			cv_message("Error opening socket log \"%s\"", filename.c_str());
		}
	}

	return socket_logs_.size();
}

} // end namespace cliver


