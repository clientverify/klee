//===----------------------------------------------------------------------===//

#ifndef FRAME_STATS_H
#define FRAME_STATS_H

#include "llvm/System/Process.h"
#include "llvm/System/TimeValue.h"
#include "klee/Statistics.h"
#include "klee/Internal/Support/Timer.h"

namespace klee {

class TimeStatIncrementer {
  uint64_t &time_;
  WallTimer timer_;

  public:
  TimeStatIncrementer(uint64_t &time) : time_(time) {}
  ~TimeStatIncrementer() { time_ += timer_.check(); }
};

struct FrameStats {
  uint64_t frame_number;       // # of frames processed
  uint64_t input_loops;        // # of input loops in this frame
  uint64_t packet_loops;       // # of packet loops in this frame
  uint64_t segment_candidates; // # of path-segments to be joined previous chains
  uint64_t duplicate_chains;   // # of path-segment chains found to be duplicates
  uint64_t valid_chains;       // # of valid path-segment chains (before queries)
  uint64_t invalid_chains;     // # of invalid path-segment chains (before queries)
  uint64_t queries;            // # of queries to solver (multiple queries per chain)
  uint64_t removed_constraints;// # of constraints removed after queries
  uint64_t sat_acc_constraints;// # of queries that passed
  uint64_t fail;               // # of queries that failed 
  WallTimer timer;             // total time spend processing frame

  // each time_{class}_{functionname} var is the total time spent each frame
  // in class::functionname()
  uint64_t time_PathManager_extendChains;
  uint64_t time_ConstraintSetFamily_applyUpdates;
  uint64_t time_ConstraintSetFamily_checkForImpliedUpdates;
  uint64_t time_ConstraintSetFamily_add;
  uint64_t time_PathSegment_query_solvertime; // total time spent in klee::solver(), called from PathSegment::query()
  uint64_t time_PathSegment_query;

  FrameStats() :
    frame_number(0),
    input_loops(0),
    packet_loops(0),
    segment_candidates(0),
    duplicate_chains(0),
    valid_chains(0),
    invalid_chains(0),
    queries(0),
    removed_constraints(0),
    sat_acc_constraints(0),
    fail(0),

    time_PathManager_extendChains(0),
    time_ConstraintSetFamily_applyUpdates(0),
    time_ConstraintSetFamily_checkForImpliedUpdates(0),
    time_ConstraintSetFamily_add(0),
    time_PathSegment_query_solvertime(0),
    time_PathSegment_query(0)
  {}

  void print() {
    uint64_t total_time = timer.check();
    llvm::sys::TimeValue elapsed(0,0), user_time(0,0), sys_time(0,0);
    llvm::sys::Process::GetTimeUsage(elapsed, user_time, sys_time);

    llvm::cout << "STATS"

      << " " << frame_number+1
      << " " << input_loops
      << " " << packet_loops
      << " " << segment_candidates
      << " " << duplicate_chains

      << " " << valid_chains
      << " " << invalid_chains
      << " " << queries
      << " " << removed_constraints
      << " " << sat_acc_constraints

      << " " << fail
      << " " << time_PathManager_extendChains
      << " " << time_ConstraintSetFamily_applyUpdates
      << " " << time_ConstraintSetFamily_checkForImpliedUpdates
      << " " << time_ConstraintSetFamily_add

      << " " << time_PathSegment_query_solvertime
      << " " << time_PathSegment_query
      << " " << total_time
      << " " << elapsed.seconds() 
      << " " << user_time.seconds() 

      << " " << sys_time.seconds() 
      << " " << llvm::sys::Process::GetMallocUsage()
      << " " << llvm::sys::Process::GetTotalMemoryUsage()
      << " " << *theStatisticManager->getStatisticByName("QueryTime")
      << " " << *theStatisticManager->getStatisticByName("CexCacheTime")

      << " " << *theStatisticManager->getStatisticByName("Queries")
      << " " << *theStatisticManager->getStatisticByName("QueriesValid")
      << " " << *theStatisticManager->getStatisticByName("QueriesInvalid")
      << " " << *theStatisticManager->getStatisticByName("QueriesConstructs")
      << " " << *theStatisticManager->getStatisticByName("QueriesCEX")

      << " " << *theStatisticManager->getStatisticByName("QueryCacheHits")
      << " " << *theStatisticManager->getStatisticByName("QueryCacheMisses")

      << "\n";
  }
   
};

extern FrameStats fstats;

} // end namespace klee

#endif // FRAME_STATS_H
