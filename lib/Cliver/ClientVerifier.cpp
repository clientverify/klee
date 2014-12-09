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
#include "cliver/TestHelper.h"
#include "CVCommon.h"
#include "ExternalHandlers.h"

#include "klee/Internal/Module/KModule.h"
#include "../lib/Core/SpecialFunctionHandler.h"
#include "../lib/Core/CoreStats.h"
#include "../lib/Solver/SolverStats.h"

#if LLVM_VERSION_CODE >= LLVM_VERSION(3, 3)
#include "llvm/IR/Module.h"
#else
#include "llvm/Module.h"
#endif

#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/Process.h"

#ifdef GOOGLE_PROFILER
#include <google/profiler.h>
#include <google/heap-checker.h>
#endif

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
RebuildSolvers("rebuild-solvers", llvm::cl::init(false));

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
	klee::Statistic merged_states("MergedStates", "MSts");
	klee::Statistic round_time("RoundTime", "RTm");
	klee::Statistic round_real_time("RoundRTime", "RRTm");
	klee::Statistic round_sys_time("RoundSTime", "RSTm");
	klee::Statistic merge_time("MergingTime", "MTm");
	klee::Statistic searcher_time("SearcherTime", "STm");
	klee::Statistic fork_time("ForkTime", "FTm");
	klee::Statistic round_instructions("RoundInsts", "RInsts");
	klee::Statistic recv_round_instructions("RecvRoundInsts", "RRInsts");
	klee::Statistic rebuild_time("RebuildTime", "RBTime");
	klee::Statistic execution_tree_time("ExecutionTreeTime", "ETTime");
	klee::Statistic edit_distance_time("EditDistanceTime","EDTm");
	klee::Statistic edit_distance_build_time("EditDistanceBuildTime","EDBdTm");
	klee::Statistic edit_distance_hint_time("EditDistanceHintTime","EDHtTm");
	klee::Statistic edit_distance_stat_time("EditDistanceStatTime","EDStTm");
	klee::Statistic stage_count("StageCount","StgCnt");
	klee::Statistic state_clone_count("StateCloneCount","StClnCnt");
	klee::Statistic state_remove_count("StateRemoveCount","StRemCnt");
	klee::Statistic edit_distance("EditDistance","ED");
	klee::Statistic edit_distance_k("EditDistanceK","EDK");
	klee::Statistic edit_distance_medoid_count("EditDistanceMedoidCount","EDMedCnt");
	klee::Statistic edit_distance_self_first_medoid("EditDistanceSelfFirstMedoid","EDSFMed");
	klee::Statistic edit_distance_self_last_medoid("EditDistanceSelfLastMedoid","EDSLMed");
	klee::Statistic edit_distance_self_socket_event("EditDistanceSelfSocketEvent","EDSLSE");
	klee::Statistic edit_distance_socket_event_first_medoid("EditDistanceSocketEventFirstMedoid","EDSEFMed");
	klee::Statistic edit_distance_socket_event_last_medoid("EditDistanceSocketEventLastMedoid","EDSELMed");
	klee::Statistic socket_event_size("SocketEventSize","SES");
	klee::Statistic valid_path_instructions("ValidPathInstructions","VPI");
	klee::Statistic symbolic_variable_count("SymbolicVariableCount","SVC");
	klee::Statistic pass_count("PassCount","PassCnt");
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
	{"cliver_test_extract_pointers", ExternalHandler_test_extract_pointers, false, CV_NULL_EVENT},
	{"cliver_select_event", ExternalHandler_select_event, false, CV_SELECT_EVENT},
	{"cliver_select", ExternalHandler_select, true, CV_SELECT_EVENT},
	{"cliver_ktest_copy", ExternalHandler_ktest_copy, true, CV_NULL_EVENT},
};

////////////////////////////////////////////////////////////////////////////////

int CVContext::next_id_ = 0;

CVContext::CVContext() : context_id_(increment_id()) {}

////////////////////////////////////////////////////////////////////////////////

ClientVerifier::ClientVerifier(std::string &input_file, bool no_output, std::string &output_dir)
  : cvstream_(new CVStream(no_output, output_dir)),
		searcher_(NULL),
		pruner_(NULL),
		merger_(NULL), 
    execution_trace_manager_(NULL),
		array_id_(0),
		round_number_(0),
		replay_objs_(NULL) {

	cvstream_->init();

  client_name_ = cvstream_->getBasename(input_file);

  // Copy inputfile to output directory
  if (CopyInputFilesToOutputDir) {
    std::string dest_name("input.bc");
    cvstream_->copyFileToOutputDirectory(input_file, dest_name);
  }

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
  if (err != NULL ) {
    CVMESSAGE(std::string(err));
  }
}

void ClientVerifier::setInterpreter(klee::Interpreter *i) {
  executor_ = static_cast<CVExecutor*>(i);
}

void ClientVerifier::initialize() {

	initialize_external_handlers(executor_);

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

  // Read socket log files
	if (SocketLogFile.empty() || read_socket_logs(SocketLogFile) == 0) {
    CVMESSAGE("No socket log files loaded");
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

  // Initialize time stats
	update_time_statistics();

  // Create and enable new StatisticRecord
	statistics_.push_back(new klee::StatisticRecord());
  klee::theStatisticManager->setCliverContext(statistics_[round_number_]);

  // Rebuild solvers
  
  CVMESSAGE("Building solver chain");
  executor_->rebuild_solvers();
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
      std::string function_name(it->getName());
      function_name += "_";
      for (llvm::Function::iterator fit = it->begin(), fie = it->end();
           fit != fie; ++fit) {
        std::string bb_name(function_name + fit->getName().str());
        basicblock_names.push_back(BBNamePair(bb_name, &(*fit)));
      }
    }
  }
  CVMESSAGE("BasicBlock count: " << basicblock_names.size());

  int count = 0;
  std::sort(basicblock_names.begin(), basicblock_names.end());
  foreach (BBNamePair &bb, basicblock_names) {
    klee::KBasicBlock *kbb = kmodule->llvm_kbasicblocks[bb.second];
    kbb->id = ++count;
    basicblock_map_[kbb->id] = kbb;
  }
}

klee::KBasicBlock* ClientVerifier::LookupBasicBlockID(int id) {
  assert(basicblock_map_.count(id));
  return basicblock_map_[id];
}

int ClientVerifier::read_socket_logs(std::vector<std::string> &logs) {

	foreach (std::string filename, logs) {
		KTest *ktest = kTest_fromFile(filename.c_str());
		if (ktest) {
			socket_events_.push_back(new SocketEventList());
			for (unsigned i=0; i<ktest->numObjects; ++i) {
        std::string obj_name(ktest->objects[i].name);
        if (obj_name == "s2c" || obj_name == "c2s")
          socket_events_.back()->push_back(new SocketEvent(ktest->objects[i]));
        else
          cv_message("Non-network log: %s, length: %d",
                     ktest->objects[i].name,
                     ktest->objects[i].numBytes);
			}

			cv_message("Opened socket log \"%s\" with %d objects",
					filename.c_str(), ktest->numObjects);

      replay_objs_ = ktest;
		} else {
			cv_error("Error opening socket log \"%s\"", filename.c_str());
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
    ev.state->set_event_flag(true);
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
  static llvm::sys::TimeValue lastNowTime(0,0),lastUserTime(0,0),lastSysTime(0,0);

  if (lastUserTime.seconds()==0 && lastUserTime.nanoseconds()==0) {
		llvm::sys::Process::GetTimeUsage(lastNowTime,lastUserTime,lastSysTime);
  } else {
		llvm::sys::TimeValue now(0,0),user(0,0),sys(0,0);
		llvm::sys::Process::GetTimeUsage(now,user,sys);
		llvm::sys::TimeValue delta = user - lastUserTime;
		llvm::sys::TimeValue deltaNow = now - lastNowTime;
		llvm::sys::TimeValue deltaSys = sys - lastSysTime;
    stats::round_time += delta.usec();
    stats::round_real_time += deltaNow.usec();
    stats::round_sys_time += deltaSys.usec();
    lastUserTime = user;
    lastNowTime = now;
    lastSysTime = sys;
  }
}

void ClientVerifier::print_stat_labels() {
*cv_message_stream << "KEY" 
    << " " << "Rnd"
    << " " << stats::round_time.getShortName()
    << " " << stats::round_real_time.getShortName()
    << " " << stats::round_sys_time.getShortName()
    << " " << klee::stats::solverTime.getShortName()
    << " " << stats::searcher_time.getShortName()
    << " " << stats::execution_tree_time.getShortName()
    << " " << stats::edit_distance_time.getShortName()
    << " " << stats::edit_distance_build_time.getShortName()
    << " " << stats::edit_distance_hint_time.getShortName()
    << " " << stats::edit_distance_stat_time.getShortName()
    << " " << stats::merge_time.getShortName()
    << " " << stats::rebuild_time.getShortName()
    << " " << stats::round_instructions.getShortName()
    << " " << stats::recv_round_instructions.getShortName()
    << " " << stats::stage_count.getShortName()
    << " " << stats::merged_states.getShortName()
    << " " << "StSz"
    << " " << "StSzTot"
    << " " << "AllcMm"
    << " " << stats::edit_distance.getShortName()
    << " " << stats::socket_event_size.getShortName()
    << " " << stats::valid_path_instructions.getShortName()
    << " " << stats::symbolic_variable_count.getShortName()
    << " " << stats::pass_count.getShortName()
    << "\n";
}

void ClientVerifier::print_current_statistics(std::string prefix) {
	klee::StatisticRecord *sr = statistics_[round_number_];
  this->print_statistic_record(sr, prefix);
}

void ClientVerifier::print_statistic_record(klee::StatisticRecord* sr,
                                            std::string &prefix) {
  double time_scale = 1.0; // 100000.
  *cv_message_stream << prefix 
    << " " << sr->getValue(stats::round_number)
    << " " << sr->getValue(stats::round_time)               /// time_scale
    << " " << sr->getValue(stats::round_real_time)          /// time_scale
    << " " << sr->getValue(stats::round_sys_time)           /// time_scale
    << " " << sr->getValue(klee::stats::solverTime)         /// time_scale
    << " " << sr->getValue(stats::searcher_time)            /// time_scale
    << " " << sr->getValue(klee::stats::queryTime)          /// time_scale
    << " " << sr->getValue(klee::stats::cexCacheTime)       /// time_scale
    << " " << sr->getValue(klee::stats::queryConstructTime) /// time_scale
    << " " << sr->getValue(klee::stats::resolveTime)        /// time_scale
    << " " << sr->getValue(stats::execution_tree_time)      /// time_scale
    << " " << sr->getValue(stats::edit_distance_time)       /// time_scale
    << " " << sr->getValue(stats::edit_distance_build_time) /// time_scale
    << " " << sr->getValue(stats::edit_distance_hint_time) /// time_scale
    << " " << sr->getValue(stats::edit_distance_stat_time) /// time_scale
    << " " << sr->getValue(stats::merge_time)               /// time_scale
    << " " << sr->getValue(stats::rebuild_time)             /// time_scale
    << " " << sr->getValue(stats::round_instructions)
    << " " << sr->getValue(stats::recv_round_instructions)
    << " " << sr->getValue(stats::stage_count)
    << " " << sr->getValue(stats::state_clone_count)
    << " " << sr->getValue(stats::state_remove_count)
    << " " << sr->getValue(stats::merged_states)
    << " " << executor()->states_size()
    << " " << CVExecutionState::next_id()
    << " " << executor()->memory_usage()
    << " " << sr->getValue(stats::edit_distance) 
    << " " << sr->getValue(stats::edit_distance_k) 
    << " " << sr->getValue(stats::edit_distance_medoid_count) 
    << " " << sr->getValue(stats::edit_distance_self_first_medoid) 
    << " " << sr->getValue(stats::edit_distance_self_last_medoid) 
    << " " << sr->getValue(stats::edit_distance_self_socket_event) 
    << " " << sr->getValue(stats::edit_distance_socket_event_first_medoid) 
    << " " << sr->getValue(stats::edit_distance_socket_event_last_medoid) 
    << " " << sr->getValue(stats::socket_event_size)
    << " " << sr->getValue(stats::valid_path_instructions)
    << " " << sr->getValue(stats::symbolic_variable_count)
    << " " << sr->getValue(stats::pass_count)
    << " " << sr->getValue(klee::stats::queries)
    << " " << sr->getValue(klee::stats::queriesInvalid)
    << " " << sr->getValue(klee::stats::queriesValid)
    << " " << sr->getValue(klee::stats::queryCacheHits)
    << " " << sr->getValue(klee::stats::queryCacheMisses)
    << " " << sr->getValue(klee::stats::queryConstructs)
    << "\n";

#ifdef GOOGLE_PROFILER
  if (ProfilerStartRoundNumber >= 0 
			&& round_number_ > ProfilerStartRoundNumber) {
    CVMESSAGE("Flushing CPU Profiler");
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
  if (RebuildSolvers)
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
  if (RebuildSolvers)
    executor_->rebuild_solvers();


#ifdef GOOGLE_PROFILER
  if (ProfilerStartRoundNumber >= 0) {
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
    executor_->setHaltExecution(true);
	}
}

klee::Interpreter *ClientVerifier::create_interpreter(const klee::Interpreter::InterpreterOptions &opts,
                                 klee::InterpreterHandler *ih) {
  return new CVExecutor(opts, ih);
}

//////////////////////////////////////////////////////////////////////////////

} // end namespace cliver

