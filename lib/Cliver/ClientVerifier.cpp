//===-- ClientVerifier.cpp---------------------------------------*- C++ -*-===//
//
// <insert license>
//
//===----------------------------------------------------------------------===//
//
// Client Verifier Class:
//
//
//===----------------------------------------------------------------------===//

#include "CVCommon.h"

#include "cliver/ClientVerifier.h"
#include "cliver/ConstraintPruner.h"
#include "cliver/CVExecutionState.h"
#include "cliver/CVExecutor.h"
#include "cliver/CVSearcher.h"
#include "cliver/CVStream.h"
#include "cliver/ExecutionObserver.h"
#include "cliver/ExecutionTree.h"
#include "cliver/NetworkManager.h"
#include "cliver/PathManager.h"
#include "cliver/StateMerger.h"
#include "cliver/TestHelper.h"

#include "klee/SpecialFunctionHandler.h"

#include "llvm/Support/Debug.h"
#include "llvm/System/Process.h"

#include "../lib/Core/CoreStats.h"

#ifdef GOOGLE_PROFILER
#include <google/profiler.h>
#include <google/heap-checker.h>

llvm::cl::opt<int>
ProfilerStartRoundNumber("profiler-start-round",llvm::cl::init(0));

llvm::cl::opt<int>
HeapCheckRoundNumber("heap-check-round",llvm::cl::init(-1));

#endif

// needed for boost::signal
void boost::throw_exception(std::exception const& e) {}

namespace cliver {

////////////////////////////////////////////////////////////////////////////////

llvm::cl::opt<int>
MaxRoundNumber("max-round",llvm::cl::init(0));

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

llvm::cl::opt<bool> DebugPrintExecutionEvents("debug-print-execution-events",
  llvm::cl::init(false));

llvm::cl::opt<bool> CountRoundInstructions("count-round-instructions",
  llvm::cl::init(false));


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
	klee::Statistic training_paths("TrainingPaths", "TPaths");
	klee::Statistic exhaustive_search_level("ExhaustiveSearchLevel", "ESLevel");
}
////////////////////////////////////////////////////////////////////////////////

struct ExternalHandlerInfo {
	const char* name;
	klee::SpecialFunctionHandler::ExternalHandler handler;
	bool has_return_value;
  ExecutionEventType event_triggered;
};

ExternalHandlerInfo external_handler_info[] = {
	//{"cliver_test_extract_pointers", ExternalHandler_test_extract_pointers, false, CV_NULL_EVENT},
	{"cliver_socket_shutdown", ExternalHandler_socket_shutdown, true, CV_SOCKET_SHUTDOWN},
	{"cliver_socket_write", ExternalHandler_socket_write, true, CV_SOCKET_WRITE},
	{"cliver_socket_read", ExternalHandler_socket_read, true, CV_SOCKET_READ},
	{"cliver_socket_create", ExternalHandler_socket_create, true, CV_SOCKET_CREATE},
	{"nuklear_merge", ExternalHandler_merge, true, CV_MERGE},
	{"klee_nuklear_XEventsQueued", ExternalHandler_XEventsQueued, true, CV_NULL_EVENT}
};

////////////////////////////////////////////////////////////////////////////////

int CVContext::next_id_ = 0;

CVContext::CVContext() : context_id_(increment_id()) {}

////////////////////////////////////////////////////////////////////////////////

class InstructionCounter : public ExecutionObserver {
 public:
  InstructionCounter() : instruction_count(0) {}

  virtual void notify(ExecutionEvent ev) {
    switch (ev.event_type) {
      case CV_STEP_INSTRUCTION: 
        instruction_count++;
        break;
      default:
        break;
    }
  }
  uint64_t instruction_count;
};

////////////////////////////////////////////////////////////////////////////////

ClientVerifier::ClientVerifier() 
  : cvstream_(new CVStream()),
		searcher_(NULL),
		pruner_(NULL),
		merger_(NULL),
		execution_tree_manager_(NULL),
		i_counter_(NULL),
		array_id_(0),
		round_number_(0) {
 
	cvstream_->init();
	handle_statistics();
	next_statistics();
}

ClientVerifier::~ClientVerifier() {
#ifdef GOOGLE_PROFILER
	ProfilerFlush();
#endif
  if (merger_)
    delete merger_;
  if (pruner_)
    delete pruner_;
  if (searcher_)
    delete searcher_;

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
  executor_ = executor;

	initialize_external_handlers(executor);

	// Load Socket files (at lease one socket file required in all modes)
	if (!SocketLogDir.empty()) {
		foreach(std::string path, SocketLogDir) {
			cvstream_->getOutFiles(path, SocketLogFile);
		}
	}

	if (SocketLogFile.empty() || read_socket_logs(SocketLogFile) == 0) {
		cv_error("Error loading socket log files, exiting now.");
	}

  if (DebugPrintExecutionEvents)
    hook(new ExecutionObserverPrinter());

  pruner_ = new ConstraintPruner();
  merger_ = new StateMerger(pruner_, this);

  searcher_ = CVSearcherFactory::create(NULL, this, merger_);
  hook(searcher_);
  
  execution_tree_manager_ = ExecutionTreeManagerFactory::create(this);
  if (execution_tree_manager_) {
    execution_tree_manager_->initialize();
    hook(execution_tree_manager_);
  }

  i_counter_ = new InstructionCounter();
  if (CountRoundInstructions) {
    hook(i_counter_);
  }

}

void ClientVerifier::initialize_external_handlers(CVExecutor *executor) {
  unsigned N = sizeof(external_handler_info)/sizeof(external_handler_info[0]);
  for (unsigned i=0; i<N; ++i) {
    ExternalHandlerInfo &hi = external_handler_info[i];
		executor->add_external_handler(hi.name, hi.handler, hi.has_return_value);
    if (hi.event_triggered != CV_NULL_EVENT) {
      executor->register_function_call_event(&hi.name, hi.event_triggered);
    }
	}
}

int ClientVerifier::read_training_paths(std::vector<std::string> &filename_list,
		PathManagerSet *path_manager_set) {
  static unsigned duplicate_training_path_count = 0;

	foreach (std::string filename, filename_list) {
		std::ifstream *is = new std::ifstream(filename.c_str(),
				std::ifstream::in | std::ifstream::binary );
		if (is != NULL && is->good()) {
			TrainingPathManager *pm = new TrainingPathManager();
			pm->read(*is, executor_);
			if (!path_manager_set->contains(pm)) {
				path_manager_set->insert(pm);
				CVMESSAGE("Path read succuessful: length " 
						<< pm->length() << ", " << pm->range() 
						<< ", File: " << filename );
			} else {
        duplicate_training_path_count++;
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
  CVMESSAGE("Duplicate Paths " << duplicate_training_path_count);
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

void ClientVerifier::hook(ExecutionObserver* observer) {
  observers_.push_back(observer);
}

void ClientVerifier::unhook(ExecutionObserver* observer) {
  observers_.remove(observer);
}

void ClientVerifier::notify_all(ExecutionEvent ev) {
  foreach (ExecutionObserver* observer, observers_) {
    observer->notify(ev);
  }

  if (ev.state)
    ev.state->notify(ev);
}

CVSearcher* ClientVerifier::searcher() {
	assert(searcher_ != NULL && "not initialized");
	return searcher_;
}

CVExecutor* ClientVerifier::executor() {
	assert(executor_ != NULL && "not initialized");
	return executor_;
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

void ClientVerifier::print_current_statistics(std::string prefix) {
	klee::StatisticRecord *sr = statistics_.back();
  *cv_message_stream << prefix 
    << " " << round_number_
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
    << " " << executor()->memory_usage()
    << " " << sr->getValue(stats::training_paths)
    << " " << sr->getValue(stats::exhaustive_search_level)
    << " " << executor()->states_size()
    << " " << i_counter_->instruction_count
    << "\n";
}

void ClientVerifier::next_round() {
  //static llvm::sys::TimeValue lastNowTime(0,0),lastUserTime(0,0);

	handle_statistics();
  executor()->update_memory_usage();
  print_current_statistics("STATS");
  round_number_++;

  // Rebuild solvers each round to keep caches fresh.
	executor_->rebuild_solvers();

#ifdef GOOGLE_PROFILER
  if (ProfilerStartRoundNumber > 0 
			&& round_number_ == ProfilerStartRoundNumber) {
		std::string profile_fn = getOutputFilename("cpu_profile.prof");
		CVDEBUG("Starting CPU Profiler");
		ProfilerStart(profile_fn.c_str());
	}

  if (ProfilerStartRoundNumber > 0 
			&& round_number_ > ProfilerStartRoundNumber) {
		ProfilerFlush();
	}
  static HeapLeakChecker* heap_checker = NULL;
  if (HeapCheckRoundNumber >= 0
			&& round_number_ == HeapCheckRoundNumber) {
    heap_checker = new HeapLeakChecker("heap_check");
    heap_checker->IgnoreObject(heap_checker);
  }
  if (HeapCheckRoundNumber >= 0
			&& round_number_ == (HeapCheckRoundNumber+1)) {
    if (!heap_checker->NoLeaks()) assert(NULL == "heap memory leak");
  }
#endif
	next_statistics();

	if (MaxRoundNumber && round_number_ > MaxRoundNumber) {
    executor_->setHaltExecution(true);
	}

  if (CountRoundInstructions) {
    unhook(i_counter_);
    delete i_counter_;
    i_counter_ = new InstructionCounter();
    hook(i_counter_);
  }

  notify_all(ExecutionEvent(CV_ROUND_START));
}

void ClientVerifier::next_statistics() {
	statistics_.push_back(new klee::StatisticRecord());
	klee::theStatisticManager->setCliverContext(statistics_.back());
}

//////////////////////////////////////////////////////////////////////////////

} // end namespace cliver

