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
#include "CVExecutionState.h"
#include "CVStream.h"
#include "CVSearcher.h"
#include "ConstraintPruner.h"
#include "StateMerger.h"
#include "TestHelper.h"
#include "llvm/Support/Debug.h"
#include "llvm/System/Process.h"
#include "klee/Statistics.h"

// needed for boost::signal
void boost::throw_exception(std::exception const& e) {}

cliver::ClientVerifier *g_client_verifier = 0;

namespace cliver {

////////////////////////////////////////////////////////////////////////////////

llvm::cl::list<std::string> SocketLogFile("socket-log",
	llvm::cl::ZeroOrMore,
	llvm::cl::ValueRequired,
	llvm::cl::desc("Specify socket log file"),
	llvm::cl::value_desc("ktest file"));

llvm::cl::list<std::string> SocketLogDir("socket-log-dir",
	llvm::cl::ZeroOrMore,
	llvm::cl::ValueRequired,
	llvm::cl::desc("Specify socket log directory"),
	llvm::cl::value_desc("ktest directory"));

llvm::cl::opt<CliverMode> g_cliver_mode("cliver-mode", 
  llvm::cl::desc("Choose the mode in which cliver should run."),
  llvm::cl::values(
    clEnumValN(DefaultMode, "default", 
      "Default mode"),
    clEnumValN(TetrinetMode, "tetrinet", 
      "Tetrinet mode"),
    clEnumValN(DefaultTrainingMode, "training", 
      "Default training mode"),
    clEnumValN(TetrinetTrainingMode, "tetrinet-training", 
      "Tetrinet training mode"),
  clEnumValEnd),
  llvm::cl::init(DefaultMode));

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

void ExternalHandler_nop (klee::Executor* executor, klee::ExecutionState *state, 
		klee::KInstruction *target, std::vector<klee::ref<klee::Expr> > &arguments) {}

ExternalHandlerInfo external_handler_info[] = {
	{"cliver_test_extract_pointers", ExternalHandler_test_extract_pointers, false},
	{"cliver_socket_shutdown", ExternalHandler_socket_shutdown, true},
	{"cliver_socket_write", ExternalHandler_socket_write, true},
	{"cliver_socket_read", ExternalHandler_socket_read, true},
	{"cliver_socket_create", ExternalHandler_socket_create, true},
	//{"cliver_training_start", ExternalHandler_nop, false},
};

////////////////////////////////////////////////////////////////////////////////

CliverEventInfo cliver_event_info[] = {
	//{CliverEvent::Network, llvm::Instruction::Call, "cliver_socket_create"},
	//{CliverEvent::Network, llvm::Instruction::Call, "cliver_socket_shutdown"},
	{CliverEvent::NetworkSend, llvm::Instruction::Call, "cliver_socket_write"},
	{CliverEvent::NetworkRecv, llvm::Instruction::Call, "cliver_socket_read"},
	//{CliverEvent::Training, llvm::Instruction::Call, "cliver_training_start"},
};

////////////////////////////////////////////////////////////////////////////////

int CVContext::next_id_ = 0;

CVContext::CVContext() : context_id_(increment_id()) {}

////////////////////////////////////////////////////////////////////////////////

ClientVerifier::ClientVerifier() 
  : cvstream_(new CVStream()), array_id_(0) {
  cvstream_->init();
	initialize_sockets();
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

void ClientVerifier::initialize_external_handlers(CVExecutor *executor) {
  unsigned N = sizeof(external_handler_info)/sizeof(external_handler_info[0]);
  for (unsigned i=0; i<N; ++i) {
    ExternalHandlerInfo &hi = external_handler_info[i];
		executor->add_external_handler(hi.name, hi.handler, hi.has_return_value);
	}
}

void ClientVerifier::initialize_sockets() {
	switch(g_cliver_mode) {
		case DefaultMode:
		case TetrinetMode:
		case XpilotMode:
		case DefaultTrainingMode:
		case TetrinetTrainingMode:
			if (!SocketLogDir.empty()) {
				foreach(std::string path, SocketLogDir) {
					cvstream_->getOutFiles(path, SocketLogFile);
				}
			}
			if (SocketLogFile.empty() || read_socket_logs(SocketLogFile) == 0) {
				goto error;
			}
			break;
    error:
			cv_error("Error loading socket log files, exiting now.");
	}
}

int ClientVerifier::read_socket_logs(std::vector<std::string> &logs) {

	foreach (std::string filename, logs) {
		KTest *ktest = kTest_fromFile(filename.c_str());
		if (ktest) {
			socket_events_.push_back(new SocketEventList());
			for (unsigned i=0; i<ktest->numObjects; ++i) {
				socket_events_.back()->push_back(new SocketEvent(ktest->objects[i]));
			}

			cv_message("Opened socket log \"%s\" with %d objects",
					filename.c_str(), ktest->numObjects);
		} else {
			cv_message("Error opening socket log \"%s\"", filename.c_str());
		}
	}

	return socket_events_.size();
}

void ClientVerifier::register_events(CVExecutor *executor) {
  unsigned N = sizeof(cliver_event_info)/sizeof(cliver_event_info[0]);
  for (unsigned i=0; i<N; ++i) {
    CliverEventInfo &ei = cliver_event_info[i];
		executor->register_event(ei);
	}

	switch(g_cliver_mode) {
		case DefaultMode:
		case TetrinetMode:
		case XpilotMode:
			pre_event_callbacks_.connect(&LogIndexSearcher::handle_pre_event);
			post_event_callbacks_.connect(&LogIndexSearcher::handle_post_event);
			break;
		case DefaultTrainingMode:
		case TetrinetTrainingMode:
			pre_event_callbacks_.connect(&TrainingMergeSearcher::handle_pre_event);
			post_event_callbacks_.connect(&TrainingMergeSearcher::handle_post_event);
			break;
	}
}

void ClientVerifier::pre_event(CVExecutionState* state, 
		CVExecutor* executor, CliverEvent::Type t) {
	pre_event_callbacks_(state, executor, t);
}

void ClientVerifier::post_event(CVExecutionState* state, 
		CVExecutor* executor, CliverEvent::Type t) {
	post_event_callbacks_(state, executor, t);
}

CVSearcher* ClientVerifier::construct_searcher() {

	pruner_ = new ConstraintPruner();

	switch(g_cliver_mode) {
		case DefaultMode:
		case TetrinetMode:
		case XpilotMode:
			merger_ = new StateMerger(pruner_);
			searcher_ = new LogIndexSearcher(new klee::DFSSearcher(), merger_);
			break;
		case DefaultTrainingMode:
		case TetrinetTrainingMode:
			merger_ = new SymbolicStateMerger(pruner_);
			searcher_ = new TrainingMergeSearcher(new klee::DFSSearcher(), merger_);
			break;
	}
	return searcher_;
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

//////////////////////////////////////////////////////////////////////////////

} // end namespace cliver

