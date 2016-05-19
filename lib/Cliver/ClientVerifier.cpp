//===-- ClientVerifier.cpp---------------------------------------*- C++ -*-===//
//
// <insert license>
//
//===----------------------------------------------------------------------===//
//
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
#include "klee/util/Mutex.h"
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
#include "llvm/ADT/StringExtras.h"

#ifdef GOOGLE_PROFILER
#include <google/profiler.h>
#include <google/heap-checker.h>
#endif

namespace klee {
extern llvm::cl::opt<unsigned> MaxMemory;
}

namespace cliver {

////////////////////////////////////////////////////////////////////////////////

llvm::cl::opt<int>
MaxRoundNumber("max-round", llvm::cl::init(0));

llvm::cl::opt<int>
MaxPassCount("max-pass-count", llvm::cl::init(0));

llvm::cl::opt<int>
OnlyVerifyFirstS2C("only-verify-first-s2c",
  llvm::cl::desc("Verify only the first N server to client messages (default=-1, i.e., all server to client messages)"),
  llvm::cl::init(-1));

llvm::cl::opt<bool>
DropS2CTLSApplicationData("drop-tls-s2c-app-data",
  llvm::cl::desc("Drop server-to-client messages that match TLS application data filter"),
  llvm::cl::init(false));

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
BasicBlockEventFlag("basic-block-event-flag", llvm::cl::init(false));

llvm::cl::opt<bool>
RebuildSolvers("rebuild-solvers", llvm::cl::init(false));

llvm::cl::opt<bool>
PrintStats("print-stats", llvm::cl::init(true));

llvm::cl::opt<bool> 
PrintExecutionEvents("print-execution-events", llvm::cl::init(false));

// Assume socket log is legitimate by default
llvm::cl::opt<bool> 
LegitimateSocketLog("legitimate-socket-log", llvm::cl::init(true));

#ifdef GOOGLE_PROFILER
llvm::cl::opt<int> 
ProfilerStartRoundNumber("profiler-start-round", llvm::cl::init(-1));

llvm::cl::opt<int> 
HeapCheckRoundNumber("heap-check-round", llvm::cl::init(-1));
#endif

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
	{"cliver_tls_predict_stdin", ExternalHandler_tls_predict_stdin, true, CV_NULL_EVENT},
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

llvm::raw_ostream &ClientVerifier::getInfoStream() const {
  return cvstream_->raw_info_stream();
}

std::string ClientVerifier::getOutputFilename(const std::string &filename) { 
  return cvstream_->getOutputFilename(filename);
}

llvm::raw_fd_ostream *ClientVerifier::openOutputFile(const std::string &filename) {
  return cvstream_->openOutputFileLLVM(filename);
}

//llvm::raw_fd_ostream *ClientVerifier::openOutputFileInSubDirectory(
//    const std::string &filename, const std::string &sub_directory) {
//  return cvstream_->openOutputFileInSubDirectory(filename, sub_directory);
//}

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

  if (PrintExecutionEvents)
    hook(new ExecutionObserverPrinter());

  pruner_ = new ConstraintPruner();
  merger_ = new StateMerger(pruner_, this);

  searcher_ = CVSearcherFactory::create(NULL, this, merger_);
  //hook(searcher_); // now done in CVExecutor, hack or OK?
  
  execution_trace_manager_ = ExecutionTraceManagerFactory::create(this);
  if (execution_trace_manager_) {
    execution_trace_manager_->initialize();
    hook(execution_trace_manager_);
  }

  // Initialize the statistics manager
  statistics_manager_.initialize();

  // Rebuild solvers
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
      unsigned bb_count = 0;
      for (llvm::Function::iterator fit = it->begin(), fie = it->end();
           fit != fie; ++fit) {
        std::string bb_name(function_name + fit->getName().str() + "_" + llvm::utostr(++bb_count));
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

  foreach(std::string filename, logs) {
    KTest *ktest = kTest_fromFile(filename.c_str());
    if (ktest) {

      socket_events_.push_back(new SocketEventList());
      unsigned s2c_count = 0;
      unsigned s2c_used = 0;
      int last_s2c_application_msg_index = INT_MAX;
      int last_s2c_application_msg_length = 0;
      for (unsigned i = 0; i < ktest->numObjects; ++i) {
        std::string obj_name(ktest->objects[i].name);
        if (obj_name == "s2c") {
          s2c_count++;
          // Two options for dropping s2c messages...
          if (DropS2CTLSApplicationData) { // ...TLS specific (by message contents)
            uint8_t first_msg_byte = ktest->objects[i].bytes[0];
            // TLS message format:
            // [byte:0 type][byte:1-2 vers][byte:3-4 length][...]
            // Drop S2C application data header == 23
            if (first_msg_byte == 23) {
              last_s2c_application_msg_index = i;
              // Extract length field
              last_s2c_application_msg_length =
                  (ktest->objects[i].bytes[3] << 8) |
                  (ktest->objects[i].bytes[4]);
              // Drop S2C rest of application data (in next SocketEvent)
            } else if (i - 1 == last_s2c_application_msg_index) {
              // check length field == ktest object(socket event) size
              if (last_s2c_application_msg_length !=
                  ktest->objects[i].numBytes) {
                cv_error("DropS2CTLSApplicationData: TLS Field Length != "
                         "message size");
              }
            } else {
              socket_events_.back()->push_back(
                  new SocketEvent(ktest->objects[i]));
              s2c_used++;
            }
          } else { // ... or by s2c message index
            if (OnlyVerifyFirstS2C == -1 || s2c_count <= OnlyVerifyFirstS2C) {
              socket_events_.back()->push_back(
                  new SocketEvent(ktest->objects[i]));
              s2c_used++;
            }
          }
        }
        if (obj_name == "c2s")
          socket_events_.back()->push_back(new SocketEvent(ktest->objects[i]));
      }

      cv_message("Opened socket log \"%s\" with %d objects", filename.c_str(),
                 ktest->numObjects);
      if (OnlyVerifyFirstS2C != -1) {
        cv_message("Skipped %d server to client messages after first %d",
                   s2c_count - (int)OnlyVerifyFirstS2C,
                   (int)OnlyVerifyFirstS2C);
      }

      if (s2c_count != s2c_used) {
        CVMESSAGE("WARNING: Verifier is not using all server-to-client "
                  "messages (see ClientVerifier::read_socket_logs for more "
                  "information)");
        CVMESSAGE("WARNING: Using " << s2c_used << " of " << s2c_count
                                    << " server-to-client messages");
      }

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
  // We set the event flag if this event relates to a state
  // To minimize the number of events, we don't set if the event_type
  // is BASICBLOCK_ENTRY (unless this is explicitly disabled by
  // BasicBlockEventFlag (false by default)
  if (ev.state) {
    if (ev.event_type != CV_BASICBLOCK_ENTRY || BasicBlockEventFlag) {
      ev.state->set_event_flag(true);
    }
  }
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

void ClientVerifier::print_current_round_stats() {
  statistics_manager_.print_round_with_short_name(*cv_message_stream,
                                                  round_number_, " ");
}

void ClientVerifier::write_all_stats() {

  std::ostream *stats_csv = cvstream_->openOutputFile("cliver.stats");

  if (stats_csv) {
    statistics_manager_.print_names(*stats_csv, ",");
    statistics_manager_.print_all_rounds(*stats_csv, ",");
    delete stats_csv;
  } else {
    cv_error("failed to print cliver.stats");
  }

  std::ostream *stage_stats_csv =
      cvstream_->openOutputFile("cliver_stage.stats");

  if (stage_stats_csv) {
    *stage_stats_csv << "ptr,";
    statistics_manager_.print_names(*stage_stats_csv, ",");
    statistics_manager_.print_all_stages(*stage_stats_csv, ",");
    delete stage_stats_csv;
  } else {
    cv_error("failed to print cliver_stage.stats");
  }

  std::ostream *summary_csv = cvstream_->openOutputFile("cliver.stats.summary");

  if (summary_csv) {
    statistics_manager_.print_all_summary(*summary_csv, ",");
    delete summary_csv;
  } else {
    cv_error("failed to print cliver.stats.summary");
  }
}

uint64_t ClientVerifier::get_round_statistic_value(int round,
                                                   const klee::Statistic& s) {
  return statistics_manager_.get_context_statistic_value(round, s);
}

void ClientVerifier::set_round(int round, SearcherStage *stage) {

  // Increment context timers before we switch to new context
  statistics_manager_.update_context_timers();

  int pass_count = statistics_manager_.get_context_statistic_value(
      round_number_, stats::pass_count);

  if (pass_count == 1) {
    stats::round_instructions_pass_one +=
      statistics_manager_.get_context_statistic_value(round_number_,
                                                      stats::round_instructions);

    stats::round_real_time_pass_one +=
      statistics_manager_.get_context_statistic_value(round_number_,
                                                      stats::round_real_time);

    stats::valid_path_instructions_pass_one +=
      statistics_manager_.get_context_statistic_value(round_number_,
                                                      stats::valid_path_instructions);
  }

  if (MaxPassCount > 0 && pass_count > MaxPassCount) {
    CVMESSAGE("Error: invalid pass count value: " << pass_count);
    executor_->setHaltExecution(true);
  }

  // Update statistic manager with new round number
  statistics_manager_.set_context(round, stage);

  // Recalculate memory usage if we are tracking it
  if (klee::MaxMemory)
    executor()->update_memory_usage();

  // Print stats from round we just finished
  if (PrintStats)
    print_current_round_stats();

  // Set new round number
  round_number_ = round;
  stats::round_number = round;
  ++stats::backtrack_count;

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

  // Check if we should halt based on command line flag
	if (MaxRoundNumber && round_number_ > MaxRoundNumber) {
    executor_->setHaltExecution(true);
	}
}

int ClientVerifier::status() {
  int status = 1;
  // socket log is valid if we've found a finished state
  bool is_valid = (executor_->finished_states().size() > 0 ? true : false);
  if (executor_->getHaltExecution()) {

    // valid path found for legitimate log: success
    if (is_valid && LegitimateSocketLog) {
      CVMESSAGE("Verifier Result: success (0): "
                << "valid path found for legitimate log");
      status = 0;
    }

    // no valid path found for non-legitimate log: success
    if (!is_valid && !LegitimateSocketLog) {
      CVMESSAGE("Verifier Result: success (0): "
                << "no valid path found for non-legitimate log");
      status = 0;
    }

    // always return success when max round number is used
    if (MaxRoundNumber != 0) {
      CVMESSAGE("Verifier Result: success (0): "
                << "Maximum round reached");
      status = 0;
    }
  } else if (executor_->state_count() == 0) {
    if (!is_valid && !LegitimateSocketLog) {
      CVMESSAGE("Verifier Result: success (0): "
                << "no valid path found for non-legitimate log");
      status = 0;
    }
  }

  // Return success if there was no log to verify
  if (SocketLogFile.size() == 0) {
    CVMESSAGE("Verifier Result: success (0): "
              << "no socket log provided");
    status = 0;
  }

  if (status != 0) {
    CVMESSAGE("Verifier Result: failure (1) ");
  }
  return status;
}

klee::Interpreter *ClientVerifier::create_interpreter(
    const klee::Interpreter::InterpreterOptions &opts,
    klee::InterpreterHandler *ih) {
  return new CVExecutor(opts, ih);
}

void ClientVerifier::WriteSearcherStageGraph() {
  auto file = cvstream_->openOutputFile("searcher_stage.graph");
  static_cast<VerifySearcher*>(searcher_)->WriteSearcherStageGraph(file);
}

//////////////////////////////////////////////////////////////////////////////

} // end namespace cliver

