//===-- ClientVerifier.cpp---------------------------------------*- C++ -*-===//
//
// <insert license>
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//

#include "ClientVerifier.h"
#include "ConstraintPruner.h"
#include "CVCommon.h"
#include "CVExecutionState.h"
#include "CVExecutor.h"
#include "CVSearcher.h"
#include "CVStream.h"
#include "NetworkManager.h"
#include "PathManager.h"
#include "StateMerger.h"
#include "TestHelper.h"

#include "llvm/Support/Debug.h"
#include "llvm/System/Process.h"

#include "klee/Statistics.h"
#include "../lib/Core/SpecialFunctionHandler.h"

#ifdef GOOGLE_PROFILER
#include <google/profiler.h>

llvm::cl::opt<int>
ProfilerStartRoundNumber("profiler-start-round",llvm::cl::init(0));
#endif

llvm::cl::opt<int>
MaxRoundNumber("max-round",llvm::cl::init(0));

// needed for boost::signal
void boost::throw_exception(std::exception const& e) {}

cliver::ClientVerifier *g_client_verifier = 0;

namespace cliver {

////////////////////////////////////////////////////////////////////////////////

llvm::cl::list<std::string> SocketLogFile("socket-log",
	llvm::cl::ZeroOrMore,
	llvm::cl::ValueRequired,
	llvm::cl::desc("Specify socket log file (.ktest)"),
	llvm::cl::value_desc("ktest file"));

llvm::cl::list<std::string> SocketLogDir("socket-log-dir",
	llvm::cl::ZeroOrMore,
	llvm::cl::ValueRequired,
	llvm::cl::desc("Specify socket log directory"),
	llvm::cl::value_desc("ktest directory"));

llvm::cl::list<std::string> TrainingPathFile("training-path-file",
	llvm::cl::ZeroOrMore,
	llvm::cl::ValueRequired,
	llvm::cl::desc("Specify a training path file (.tpath)"),
	llvm::cl::value_desc("tpath directory"));

llvm::cl::list<std::string> TrainingPathDir("training-path-dir",
	llvm::cl::ZeroOrMore,
	llvm::cl::ValueRequired,
	llvm::cl::desc("Specify directory containint .tpath files"),
	llvm::cl::value_desc("tpath directory"));


llvm::cl::opt<CliverMode> g_cliver_mode("cliver-mode", 
  llvm::cl::desc("Choose the mode in which cliver should run."),
  llvm::cl::values(
    clEnumValN(DefaultMode, "default", 
      "Default mode"),
    clEnumValN(TetrinetMode, "tetrinet", 
      "Tetrinet mode"),
    clEnumValN(DefaultTrainingMode, "training", 
      "Default training mode"),
    clEnumValN(OutOfOrderTrainingMode, "out-of-order-training", 
      "Default training mode"),
    clEnumValN(TetrinetTrainingMode, "tetrinet-training", 
      "Tetrinet training mode"),
    clEnumValN(VerifyWithTrainingPaths, "verify-with-paths", 
      "Verify with training paths"),
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
	klee::Statistic searcher_time("SearcherTime", "Stime");
	klee::Statistic fork_time("ForkTime", "Ftime");
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
	{CliverEvent::Network, llvm::Instruction::Call, "cliver_socket_shutdown"},
	{CliverEvent::NetworkSend, llvm::Instruction::Call, "cliver_socket_write"},
	{CliverEvent::NetworkRecv, llvm::Instruction::Call, "cliver_socket_read"},
	//{CliverEvent::Training, llvm::Instruction::Call, "cliver_training_start"},
};

////////////////////////////////////////////////////////////////////////////////

int CVContext::next_id_ = 0;

CVContext::CVContext() : context_id_(increment_id()) {}

////////////////////////////////////////////////////////////////////////////////

ClientVerifier::ClientVerifier() 
  : cvstream_(new CVStream()),
		searcher_(NULL),
		pruner_(NULL),
		merger_(NULL),
		array_id_(0) {
 
	cvstream_->init();
	handle_statistics();
	next_statistics();
}

ClientVerifier::~ClientVerifier() {
#ifdef GOOGLE_PROFILER
	ProfilerFlush();
#endif
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

void ClientVerifier::initialize(CVExecutor *executor) {
	initialize_external_handlers(executor);
	register_events(executor);

	// Load Socket files (at lease one socket file required in all modes)
	if (!SocketLogDir.empty()) {
		foreach(std::string path, SocketLogDir) {
			cvstream_->getOutFiles(path, SocketLogFile);
		}
	}

	if (SocketLogFile.empty() || read_socket_logs(SocketLogFile) == 0) {
		cv_error("Error loading socket log files, exiting now.");
	}

	switch(g_cliver_mode) {
		case DefaultMode:
		case TetrinetMode:
		case XpilotMode:

			// Construct searcher
			pruner_ = new ConstraintPruner();
			merger_ = new StateMerger(pruner_);
			searcher_ = new LogIndexSearcher(new klee::DFSSearcher(), merger_);

			// Set event callbacks
			//pre_event_callbacks_.connect(&LogIndexSearcher::handle_pre_event);
			//post_event_callbacks_.connect(&LogIndexSearcher::handle_post_event);
			pre_event_callback_func_   = &LogIndexSearcher::handle_pre_event;
			post_event_callback_func_ = &LogIndexSearcher::handle_post_event;
			break;

		case DefaultTrainingMode:

			// Construct searcher
			pruner_ = new ConstraintPruner();
			merger_ = new StateMerger(pruner_);
			searcher_ = new TrainingSearcher(NULL, merger_);

			// Set event callbacks
			//pre_event_callbacks_.connect(&TrainingSearcher::handle_pre_event);
			//post_event_callbacks_.connect(&TrainingSearcher::handle_post_event);
			pre_event_callback_func_   = &TrainingSearcher::handle_pre_event;
			post_event_callback_func_ = &TrainingSearcher::handle_post_event;
			break;

		case VerifyWithTrainingPaths: {

			// Read training paths
			PathManagerSet* training_paths = new PathManagerSet();
			if (!TrainingPathDir.empty()) {
				foreach(std::string path, TrainingPathDir) {
					cvstream_->getFiles(path, ".tpath", TrainingPathFile);
				}
			}
			if (TrainingPathFile.empty() || read_training_paths(TrainingPathFile,
						training_paths) == 0) {
				cv_error("Error reading training path files, exiting now.");
			} 

			// Construct searcher
			pruner_ = new ConstraintPruner();
			merger_ = new StateMerger(pruner_);
			searcher_ = new VerifySearcher(NULL, merger_, training_paths);

			// Set event callbacks
			//pre_event_callbacks_.connect(&VerifySearcher::handle_pre_event);
			//post_event_callbacks_.connect(&VerifySearcher::handle_post_event);
			pre_event_callback_func_   = &VerifySearcher::handle_pre_event;
			post_event_callback_func_ = &VerifySearcher::handle_post_event;
			break;
		}

		case TetrinetTrainingMode:
			cv_error("Tetrinet Training mode is unsupported");
			break;

		//case OutOfOrderTrainingMode:

		//	// Construct searcher
		//	pruner_ = new ConstraintPruner();
		//	merger_ = new SymbolicStateMerger(pruner_);
		//	searcher_ 
		//		= new OutOfOrderTrainingSearcher(NULL, merger_);

		//	// Set event callbacks
		//	pre_event_callbacks_.connect(&OutOfOrderTrainingSearcher::handle_pre_event);
		//	post_event_callbacks_.connect(&OutOfOrderTrainingSearcher::handle_post_event);
		//	break;
	}

}

void ClientVerifier::initialize_external_handlers(CVExecutor *executor) {
  unsigned N = sizeof(external_handler_info)/sizeof(external_handler_info[0]);
  for (unsigned i=0; i<N; ++i) {
    ExternalHandlerInfo &hi = external_handler_info[i];
		executor->add_external_handler(hi.name, hi.handler, hi.has_return_value);
	}
}

int ClientVerifier::read_training_paths(std::vector<std::string> &filename_list,
		PathManagerSet *path_manager_set) {

	foreach (std::string filename, filename_list) {
		std::ifstream *is = new std::ifstream(filename.c_str(),
				std::ifstream::in | std::ifstream::binary );
		if (is != NULL && is->good()) {
			TrainingPathManager *pm = new TrainingPathManager();
			pm->read(*is);
			if (!path_manager_set->contains(pm)) {
				path_manager_set->insert(pm);
				CVMESSAGE("Path read succuessful: length " 
						<< pm->length() << ", " << pm->range() 
						<< ", File: " << filename );
			} else {
				TrainingPathManager *merged_pm 
					= static_cast<TrainingPathManager*>(path_manager_set->merge(pm));
				if (merged_pm)
					CVMESSAGE("Path already exists: messages "
							<< merged_pm->socket_events().size() << ", length " 
							<< merged_pm->length() << ", " << merged_pm->range() );
				delete pm;
			}
			delete is;
		}
	}
	return path_manager_set->size();
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
}

void ClientVerifier::pre_event(CVExecutionState* state, 
		CVExecutor* executor, CliverEvent::Type t) {
	//pre_event_callbacks_(state, executor, t);
	pre_event_callback_func_(state, executor, t);
}

void ClientVerifier::post_event(CVExecutionState* state, 
		CVExecutor* executor, CliverEvent::Type t) {
	//post_event_callbacks_(state, executor, t);
	post_event_callback_func_(state, executor, t);
}

CVSearcher* ClientVerifier::searcher() {
	assert(searcher_ != NULL && "not initialized");
	return searcher_;
}

void ClientVerifier::handle_statistics() {

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
  //static llvm::sys::TimeValue lastNowTime(0,0),lastUserTime(0,0);

	handle_statistics();
	klee::StatisticRecord *sr = statistics_.back();

  *cv_message_stream << "STATS " << ++statistic_round
    << " " << sr->getValue(stats::active_states)
    << " " << sr->getValue(stats::merged_states)
    << " " << sr->getValue(stats::pruned_constraints)
    << " " << sr->getValue(stats::round_time) / 1000000.
    << " " << sr->getValue(stats::round_real_time) / 1000000.
    << " " << sr->getValue(stats::prune_time) / 1000000.
    << " " << sr->getValue(stats::merge_time) / 1000000.
    << " " << sr->getValue(stats::searcher_time) / 1000000.
    << " " << sr->getValue(klee::stats::solverTime) / 1000000.
    << " " << sr->getValue(stats::fork_time) / 1000000.
    << " " << llvm::sys::Process::GetTotalMemoryUsage()
    << "\n";


  // Rebuild solvers each round to keep caches fresh.                                                                                                                                                                
	g_executor->rebuild_solvers();

	if (MaxRoundNumber && statistic_round > MaxRoundNumber) {
		// need cleaner exit
		exit(1);
	}

#ifdef GOOGLE_PROFILER
  if (ProfilerStartRoundNumber > 0 
			&& statistic_round == ProfilerStartRoundNumber) {
		std::string profile_fn = getOutputFilename("cpu_profile.prof");
		CVDEBUG("Starting CPU Profiler");
		ProfilerStart(profile_fn.c_str());
	}

  if (ProfilerStartRoundNumber > 0 
			&& statistic_round > ProfilerStartRoundNumber) {
		ProfilerFlush();
	}
#endif
	next_statistics();
}

void ClientVerifier::next_statistics() {
	statistics_.push_back(new klee::StatisticRecord());
	klee::theStatisticManager->setCliverContext(statistics_.back());
}

//////////////////////////////////////////////////////////////////////////////

} // end namespace cliver

