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
#include "llvm/Support/Debug.h"
#include "llvm/System/Process.h"
#include "klee/Statistics.h"

#include <string>
#include <boost/foreach.hpp>
#define foreach BOOST_FOREACH 

cliver::ClientVerifier *g_client_verifier = 0;

namespace cliver {

////////////////////////////////////////////////////////////////////////////////

llvm::cl::list<std::string> SocketLogFile("socket-log",
		llvm::cl::ZeroOrMore,
		llvm::cl::ValueRequired,
		llvm::cl::desc("Specify a socket log file"),
		llvm::cl::value_desc("ktest file"));

////////////////////////////////////////////////////////////////////////////////

namespace stats {
	klee::Statistic active_states("ActiveStates", "AStates");
	klee::Statistic merged_states("MergedStates", "MStates");
	klee::Statistic round_time("RoundTime", "RTime");
	klee::Statistic round_real_time("RoundRealTime", "RRTime");
	klee::Statistic merge_time("MergingTime", "MTime");
	klee::Statistic prune_time("PruningTime", "PTime");
	klee::Statistic pruned_constraints("PrunedConstraints", "prunes");
}

////////////////////////////////////////////////////////////////////////////////

int CVContext::next_id_ = 0;

CVContext::CVContext() : context_id_(increment_id()) {}

////////////////////////////////////////////////////////////////////////////////

ClientVerifier::ClientVerifier() 
  : cvstream_(new CVStream()) {
  cvstream_->init();
	load_socket_files();
	handle_statistics();
}

ClientVerifier::~ClientVerifier() {
	delete cvstream_;
}	

std::ostream &ClientVerifier::getInfoStream() const { 
  return cvstream_->info_stream();
}

std::string ClientVerifier::getOutputFilename(const std::string &filename) { 
  return cvstream_->getOutputFilename(filename);
}

std::ostream *ClientVerifier::openOutputFile(const std::string &filename) {
  return cvstream_->openOutputFile(filename);
}

void ClientVerifier::incPathsExplored() {
  paths_explored_++;
}

void ClientVerifier::processTestCase(const klee::ExecutionState &state, 
    const char *err, const char *suffix) {
}

void ClientVerifier::load_socket_files() {
	// Load socket log files
	if (SocketLogFile.size() > 0) {
		if (load_socket_logs() == 0) {
			cv_error("Error loading socket log files, exiting now.");
		}
	}
}

void ClientVerifier::initialize_external_handlers(CVExecutor *executor) {
	// init testing helper handlers
	executor->add_external_handler(
			"cliver_test_extract_pointers", ExternalHandler_test_extract_pointers, false);
	// Add socket external handlers
	executor->add_external_handler("cliver_socket_shutdown", 
			ExternalHandler_socket_shutdown);
	executor->add_external_handler("cliver_socket_write", 
			ExternalHandler_socket_write);
	executor->add_external_handler("cliver_socket_read", 
			ExternalHandler_socket_read);
	executor->add_external_handler("cliver_socket_create", 
			ExternalHandler_socket_create);
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

void ClientVerifier::handle_statistics() {
	statistics_.push_back(new klee::StatisticRecord());
	klee::theStatisticManager->setCliverContext(statistics_.back());

  static llvm::sys::TimeValue lastNowTime(0,0),lastUserTime(0,0);

  if (lastUserTime.seconds()==0 && lastUserTime.nanoseconds()==0) {
		llvm::sys::TimeValue sys(0,0);
		llvm::sys::Process::GetTimeUsage(lastNowTime,lastUserTime,sys);
  } else {
		llvm::sys::TimeValue now(0,0),user(0,0),sys(0,0);
		llvm::sys::Process::GetTimeUsage(now,user,sys);
		llvm::sys::TimeValue delta = user - lastUserTime;
		llvm::sys::TimeValue deltaNow = now - lastNowTime;
    stats::round_time += delta.usec();
    stats::round_real_time += deltaNow.usec();
    lastUserTime = user;
    lastNowTime = now;
  }
}

void ClientVerifier::print_current_statistics() {
	static unsigned statistic_round = 0;
  static llvm::sys::TimeValue lastNowTime(0,0),lastUserTime(0,0);
	klee::StatisticRecord *sr = statistics_.back();

	handle_statistics();

  *cv_message_stream << "STATS " << ++statistic_round
    << " " << sr->getValue(stats::active_states)
    << " " << sr->getValue(stats::merged_states)
    << " " << sr->getValue(stats::pruned_constraints)
    << " " << sr->getValue(stats::round_time) / 1000000.
    << " " << sr->getValue(stats::round_real_time) / 1000000.
    << " " << sr->getValue(stats::prune_time) / 1000000.
    << " " << sr->getValue(stats::merge_time) / 1000000.
    << " " << 0
    << " " << 0
    << " " << 0 
    << " " << llvm::sys::Process::GetTotalMemoryUsage()
    << "\n";
}

} // end namespace cliver

