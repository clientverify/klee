//===-- ClientVerifier.cpp---------------------------------------*- C++ -*-===//
//
// <insert license>
//
//===----------------------------------------------------------------------===//
//
// TODO Print name for each stats column when we start
// TODO CVContext needed?
//
//===----------------------------------------------------------------------===//

#include "cliver/ClientVerifier.h"
#include "cliver/ConstraintPruner.h"
#include "cliver/CVExecutionState.h"
#include "cliver/CVExecutor.h"
#include "cliver/CVSearcher.h"
#include "cliver/CVStream.h"
#include "cliver/ExecutionObserver.h"
#include "cliver/ExecutionTraceManager.h"
#include "cliver/NetworkManager.h"
#include "cliver/StateMerger.h"
#include "CVCommon.h"
#include "ExternalHandlers.h"

#include "klee/Internal/Module/KModule.h"
#include "klee/SpecialFunctionHandler.h"
#include "../lib/Core/CoreStats.h"

#include "llvm/Module.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/System/Process.h"

#ifdef GOOGLE_PROFILER
#include <google/profiler.h>
#include <google/heap-checker.h>
#endif

// Define somewhere else?
void boost::throw_exception(std::exception const& e) {}

namespace cliver {

////////////////////////////////////////////////////////////////////////////////

llvm::cl::opt<int>
MaxRoundNumber("max-round", llvm::cl::init(0));

llvm::cl::list<std::string> 
SocketLogFile("socket-log",
  llvm::cl::ZeroOrMore,
  llvm::cl::ValueRequired,
  llvm::cl::desc("Specify socket log file (.ktest)"),
  llvm::cl::value_desc("ktest file"));

llvm::cl::list<std::string> 
SocketLogDir("socket-log-dir",
  llvm::cl::ZeroOrMore,
  llvm::cl::ValueRequired,
  llvm::cl::desc("Specify socket log directory"),
  llvm::cl::value_desc("ktest directory"));

llvm::cl::opt<bool> 
CopyInputFilesToOutputDir("copy-input-files-to-output-dir", llvm::cl::init(false));

llvm::cl::opt<bool> 
DebugPrintExecutionEvents("debug-print-execution-events", llvm::cl::init(false));

#ifdef GOOGLE_PROFILER
llvm::cl::opt<int> 
ProfilerStartRoundNumber("profiler-start-round", llvm::cl::init(-1));

llvm::cl::opt<int> 
HeapCheckRoundNumber("heap-check-round", llvm::cl::init(-1));
#endif

////////////////////////////////////////////////////////////////////////////////

namespace stats {
	klee::Statistic round_number("RoundNumber", "Rn");
	klee::Statistic active_states("ActiveStates", "ASts");
	klee::Statistic merged_states("MergedStates", "MSts");
	klee::Statistic round_time("RoundTime", "RTm");
	klee::Statistic round_real_time("RoundRealTime", "RRTm");
	klee::Statistic merge_time("MergingTime", "MTm");
	klee::Statistic prune_time("PruningTime", "PTm");
	klee::Statistic pruned_constraints("PrunedConstraints", "Prn");
	klee::Statistic searcher_time("SearcherTime", "STm");
	klee::Statistic fork_time("ForkTime", "FTm");
	klee::Statistic round_instructions("RoundInsts", "RInsts");
	klee::Statistic rebuild_time("RebuildTime", "RBTime");
	klee::Statistic execution_tree_time("ExecutionTreeTime", "ETTime");
	klee::Statistic execution_tree_extend_time("EditDistanceExtendTime","EDExTm");
	klee::Statistic edit_distance_compute_time("EditDistanceComputeTime","EDCoTm");
	klee::Statistic edit_distance_build_time("EditDistanceBuildTime","EDBdTm");
	klee::Statistic edit_distance_tree_size("EditDistanceTreeSize","EDTSz");
	klee::Statistic edit_distance_final_k("EditDistanceFinalK","EDFK");
	klee::Statistic edit_distance_min_score("EditDistanceMinScore","EDMS");
	klee::Statistic stage_count("StageCount","StgCnt");
	klee::Statistic self_path_edit_distance("SelfPathEditDistance","SpED");
	klee::Statistic training_time("TrainingTime","TrTm");
}

////////////////////////////////////////////////////////////////////////////////

struct ExternalHandlerInfo {
	const char* name;
	klee::SpecialFunctionHandler::ExternalHandler handler;
	bool has_return_value;
  ExecutionEventType event_triggered;
};

ExternalHandlerInfo external_handler_info[] = {
	{"cliver_socket_shutdown", ExternalHandler_socket_shutdown, true, CV_SOCKET_SHUTDOWN},
	{"cliver_socket_write", ExternalHandler_socket_write, true, CV_SOCKET_WRITE},
	{"cliver_socket_read", ExternalHandler_socket_read, true, CV_SOCKET_READ},
	{"cliver_socket_create", ExternalHandler_socket_create, true, CV_SOCKET_CREATE},
	{"nuklear_merge", ExternalHandler_merge, true, CV_MERGE},
	{"klee_nuklear_XEventsQueued", ExternalHandler_XEventsQueued, true, CV_NULL_EVENT},
	{"cliver_print", ExternalHandler_CliverPrint, false, CV_NULL_EVENT},
	{"cliver_disable_tracking", ExternalHandler_DisableBasicBlockTracking, false, CV_NULL_EVENT},
	{"cliver_enable_tracking", ExternalHandler_EnableBasicBlockTracking, false, CV_NULL_EVENT},
	{"cliver_finish", ExternalHandler_Finish, false, CV_FINISH},
	{"cliver_select_event", ExternalHandler_select_event, false, CV_SELECT_EVENT},
};

////////////////////////////////////////////////////////////////////////////////

int CVContext::next_id_ = 0;

CVContext::CVContext() : context_id_(increment_id()) {}

////////////////////////////////////////////////////////////////////////////////

ClientVerifier::ClientVerifier(std::string* input_filename)
  : cvstream_(new CVStream()),
		searcher_(NULL),
		pruner_(NULL),
		merger_(NULL),
		execution_trace_manager_(NULL),
		array_id_(0),
		round_number_(0) {

	cvstream_->init();

  if (input_filename)
    client_name_ = cvstream_->getBasename(*input_filename);

  // Copy inputfile to output directory
  if (CopyInputFilesToOutputDir) {
    std::string dest_name("input.bc");
    cvstream_->copyFileToOutputDirectory(*input_filename, dest_name);
  }

  // Initialize time stats
	update_time_statistics();

  // Create and enable new StatisticRecord
	statistics_.push_back(new klee::StatisticRecord());
  klee::theStatisticManager->setCliverContext(statistics_[round_number_]);
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

std::ostream *ClientVerifier::openOutputFileInSubDirectory(
    const std::string &filename, const std::string &sub_directory) {
  return cvstream_->openOutputFileInSubDirectory(filename, sub_directory);
}

void ClientVerifier::getFiles(std::string path, std::string suffix,
                              std::vector<std::string> &results) {
  return cvstream_->getFiles(path, suffix, results);
}

void ClientVerifier::getFilesRecursive(std::string path, std::string suffix,
                                       std::vector<std::string> &results) {
  return cvstream_->getFilesRecursive(path, suffix, results);
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

  // Copy input files if indicated and rename to prevent duplicates
  if (CopyInputFilesToOutputDir) {
    unsigned count=0;
    foreach (std::string path, SocketLogFile) {
      std::stringstream dest_name;
      dest_name << "socket_" << std::setw(3) << std::setfill('0') << count;
      dest_name << ".ktest";
      cvstream_->copyFileToOutputDirectory(path, dest_name.str());
      count++;
    }
  }

	if (SocketLogFile.empty() || read_socket_logs(SocketLogFile) == 0) {
		cv_error("Error loading socket log files, exiting now.");
	}

  assign_basic_block_ids();

  if (DebugPrintExecutionEvents)
    hook(new ExecutionObserverPrinter());

  pruner_ = new ConstraintPruner();
  merger_ = new StateMerger(pruner_, this);

  searcher_ = CVSearcherFactory::create(NULL, this, merger_);
  hook(searcher_);
  
  execution_trace_manager_ = ExecutionTraceManagerFactory::create(this);
  if (execution_trace_manager_) {
    execution_trace_manager_->initialize();
    hook(execution_trace_manager_);
  }

  print_stat_labels();
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

void ClientVerifier::assign_basic_block_ids() {
  klee::KModule *kmodule = executor_->get_kmodule();
  llvm::Module *module = kmodule->module;

  typedef std::pair<std::string, llvm::BasicBlock*> BBNamePair;
  std::vector< BBNamePair > basicblock_names;

  for (llvm::Module::iterator it = module->begin(), ie = module->end();
      it != ie; ++it) {

    if (!it->isDeclaration()) {
      std::string function_name(it->getNameStr());
      function_name += "_";
      for (llvm::Function::iterator fit = it->begin(), fie = it->end();
           fit != fie; ++fit) {
        std::string bb_name(function_name + fit->getNameStr());
        basicblock_names.push_back(BBNamePair(bb_name, &(*fit)));
      }
    }
  }

  int count = 0;
  std::sort(basicblock_names.begin(), basicblock_names.end());
  foreach (BBNamePair &bb, basicblock_names) {
    //CVMESSAGE(bb.first);
    klee::KBasicBlock *kbb = kmodule->llvm_kbasicblocks[bb.second];
    kbb->id = ++count;
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
  //if (ev.state)
  //  ev.state->notify(ev);
}

CVSearcher* ClientVerifier::searcher() {
	assert(searcher_ != NULL && "not initialized");
	return searcher_;
}

CVExecutor* ClientVerifier::executor() {
	assert(executor_ != NULL && "not initialized");
	return executor_;
}

ExecutionTraceManager* ClientVerifier::execution_trace_manager() {
	assert(execution_trace_manager_ != NULL && "not initialized");
	return execution_trace_manager_;
}

void ClientVerifier::update_time_statistics() {
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

void ClientVerifier::print_stat_labels() {
*cv_message_stream << "KEY" 
    << " " << "Rnd"
    << " " << stats::active_states.getShortName()
    << " " << stats::merged_states.getShortName()
    << " " << stats::pruned_constraints.getShortName()
    << " " << stats::round_time.getShortName()
    << " " << stats::round_real_time.getShortName()
    << " " << stats::prune_time.getShortName()
    << " " << stats::merge_time.getShortName()
    << " " << stats::searcher_time.getShortName()
    << " " << klee::stats::solverTime.getShortName()
    << " " << stats::fork_time.getShortName()
    << " " << stats::rebuild_time.getShortName()
    << " " << stats::round_instructions.getShortName()
    << " " << "StSz"
    << " " << "StSzTot"
    << " " << "AllcMm"
    << " " << stats::execution_tree_time.getShortName()
    << " " << stats::execution_tree_extend_time.getShortName()
    << " " << stats::edit_distance_compute_time.getShortName()
    << " " << stats::edit_distance_build_time.getShortName()
    << " " << stats::edit_distance_tree_size.getShortName()
    << " " << stats::edit_distance_final_k.getShortName()
    << " " << stats::edit_distance_min_score.getShortName()
    << " " << stats::stage_count.getShortName()
    << " " << stats::self_path_edit_distance.getShortName()
    << " " << stats::training_time.getShortName()
    << "\n";
}

void ClientVerifier::print_current_statistics(std::string prefix) {
	klee::StatisticRecord *sr = statistics_[round_number_];
  this->print_statistic_record(sr, prefix);
}

void ClientVerifier::print_statistic_record(klee::StatisticRecord* sr,
                                            std::string &prefix) {
  *cv_message_stream << prefix 
    << " " << sr->getValue(stats::round_number)
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
    << " " << sr->getValue(stats::rebuild_time) / 1000000.
    << " " << sr->getValue(stats::round_instructions)
    << " " << executor()->states_size()
    << " " << CVExecutionState::next_id()
    << " " << executor()->memory_usage()
    << " " << sr->getValue(stats::execution_tree_time) / 1000000.
    << " " << sr->getValue(stats::execution_tree_extend_time) / 1000000.
    << " " << sr->getValue(stats::edit_distance_compute_time) / 1000000.
    << " " << sr->getValue(stats::edit_distance_build_time) / 1000000.
    << " " << sr->getValue(stats::edit_distance_tree_size)
    << " " << sr->getValue(stats::edit_distance_final_k)
    << " " << sr->getValue(stats::edit_distance_min_score)
    << " " << sr->getValue(stats::stage_count)
    << " " << sr->getValue(stats::self_path_edit_distance)
    << " " << sr->getValue(stats::training_time) / 1000000.
    << "\n";

#ifdef GOOGLE_PROFILER
  if (ProfilerStartRoundNumber >= 0 
			&& round_number_ > ProfilerStartRoundNumber) {
    // XXX TBD: reimplement for set_round
    cv_error("profiler-start-round not implemented");
	  ProfilerFlush();
  }
#endif
}

void ClientVerifier::print_current_stats_and_reset() {
  // Update clocks with time spent in most recent round
	update_time_statistics();

  // Recalculate memory usage
  executor()->update_memory_usage();

  // Rebuild solvers each round change to keep caches fresh.
	executor_->rebuild_solvers();

  // Print current statistics
  std::string prefix("STATS");
  this->print_statistic_record(statistics_[round_number_], prefix);

  // Delete current StatisticRecord and create a new one
  delete statistics_[round_number_];
  statistics_[round_number_] = new klee::StatisticRecord();
  klee::theStatisticManager->setCliverContext(statistics_[round_number_]);

  stats::round_number += round_number_;
}

void ClientVerifier::print_all_stats() {
  std::string prefix("STATS");

  foreach (klee::StatisticRecord* sr, statistics_) {
    print_statistic_record(sr, prefix);
  }
}

void ClientVerifier::set_round(int round) {
  // Update clocks with time spent in most recent round
	update_time_statistics();

  // Recalculate memory usage
  executor()->update_memory_usage();

  // Print current stats
  std::string prefix("STAGE");
  this->print_current_statistics(prefix);

  // Set new round number
  round_number_ = round;

  // Rebuild solvers each round change to keep caches fresh.
	executor_->rebuild_solvers();


#ifdef GOOGLE_PROFILER
  if (ProfilerStartRoundNumber >= 0) {
    // XXX TBD: reimplement for set_round
    cv_error("profiler-start-round not implemented");
		if (round_number_ == ProfilerStartRoundNumber) {
      std::string profile_fn = getOutputFilename("cpu_profile.prof");
      CVMESSAGE("Starting CPU Profiler");
      ProfilerStart(profile_fn.c_str());
    }
		if (round_number_ > ProfilerStartRoundNumber) {
      ProfilerFlush();
    }
  }

  static HeapLeakChecker* heap_checker = NULL;
  if (HeapCheckRoundNumber >= 0) {
    // XXX TBD: reimplement for set_round
    cv_error("heap-check-round not implemented");
	  if (round_number_ == HeapCheckRoundNumber) {
      heap_checker = new HeapLeakChecker("heap_check");
      heap_checker->IgnoreObject(heap_checker);
    }
		if (round_number_ == (HeapCheckRoundNumber+1)) {
      if (!heap_checker->NoLeaks()) 
        assert(NULL == "heap memory leak");
    }
  }
#endif

  assert(round_number_ <= statistics_.size());

  if (round_number_ == statistics_.size()) {
    statistics_.push_back(new klee::StatisticRecord());
  }

  klee::theStatisticManager->setCliverContext(statistics_[round_number_]);
  stats::round_number = round_number_;

	if (MaxRoundNumber && round_number_ > MaxRoundNumber) {
    // XXX TBD: reimplement for set_round
    cv_error("max-round not implemented");
    CVMESSAGE("Exiting early, max round is " << MaxRoundNumber);
    executor_->setHaltExecution(true);
	}
}

//////////////////////////////////////////////////////////////////////////////

} // end namespace cliver

