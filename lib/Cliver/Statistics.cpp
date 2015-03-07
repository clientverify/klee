//===-- Statistics.cpp ------------------------------------------*- C++ -*-===//
//
// <insert license>
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//

#include "cliver/Statistics.h"
#include "../lib/Solver/SolverStats.h"
#include "../lib/Core/CoreStats.h"

#include <ostream>
#include <iomanip>

namespace stats {
  #define XSTAT(X,NAME,SHORTNAME) klee::Statistic X( #NAME, #SHORTNAME );
  #include "cliver/stats.inc"
  #undef XSTAT
}

CVStatisticsManager::CVStatisticsManager() :
  context_now_time_(0,0), context_user_time_(0,0), context_sys_time_(0,0) {}

void CVStatisticsManager::initialize() {
  using namespace stats;

  // Cliver stats
  #define XSTAT(X,NAME,SHORTNAME) statistics_.push_back(&( X ));
  #include "cliver/stats.inc"
  #undef XSTAT

  // klee stats
  statistics_.push_back(&(klee::stats::solverTime));
  statistics_.push_back(&(klee::stats::queryTime));
  statistics_.push_back(&(klee::stats::cexCacheTime));
  statistics_.push_back(&(klee::stats::queryConstructTime));
  statistics_.push_back(&(klee::stats::resolveTime));
  statistics_.push_back(&(klee::stats::queries));
  statistics_.push_back(&(klee::stats::queriesInvalid));
  statistics_.push_back(&(klee::stats::queriesValid));
  statistics_.push_back(&(klee::stats::queryCacheHits));
  statistics_.push_back(&(klee::stats::queryCacheMisses));
  statistics_.push_back(&(klee::stats::queryConstructs));

  llvm::sys::Process::GetTimeUsage(context_now_time_,context_user_time_,context_sys_time_);

  // set up the first StatisticRecord for cliver stats
  set_context(0);
}

void CVStatisticsManager::set_context(unsigned index) {

  // Increment context timers before we switch to new context
  update_context_timers();

  // Allocate new StatisticRecord if this a new round
  klee::StatisticRecord *sr = NULL;
  if (index == statistic_records_.size()) {
    sr = new klee::StatisticRecord();
    statistic_records_.push_back(sr);
  } else {
    sr = statistic_records_[index];
  }

  // Set new cliver context within KLEE stats
  klee::theStatisticManager->setCliverContext(sr);
}

// Add time elapsed since last time this function was run to stats::round_time
// stats::round_real_time and stats::round_sys_time
void CVStatisticsManager::update_context_timers() {
  // Get updated time usage
  llvm::sys::TimeValue now(0,0),user(0,0),sys(0,0);
  llvm::sys::Process::GetTimeUsage(now,user,sys);

  // Calculate how much time has pass since last update
  llvm::sys::TimeValue delta    = user - context_user_time_;
  llvm::sys::TimeValue deltaNow = now - context_now_time_;
  llvm::sys::TimeValue deltaSys = sys - context_sys_time_;

  // Add usage to time stats (this will accumulate if we execute in one
  // round, advance, and then return via backtracking)
  stats::round_time      += delta.usec();
  stats::round_real_time += deltaNow.usec();
  stats::round_sys_time  += deltaSys.usec();

  // Save current usage
  context_user_time_ = user;
  context_now_time_  = now;
  context_sys_time_  = sys;
}

// Print all statistics we are tracking with a given sep and a newline
// between contexts
void CVStatisticsManager::print_all_rounds(std::ostream &os, std::string sep) {
  for (unsigned i=0; i<statistic_records_.size(); ++i)
    print_round(os, i, sep);
}

// Print statistics for a given round with seperator and newline at the end
void CVStatisticsManager::print_round(std::ostream &os, unsigned index, std::string sep) {
  klee::StatisticRecord *sr = statistic_records_[index];

  auto it = begin(statistics_), ie = end(statistics_);
  auto it_last = --end(statistics_); // don't print sep after this element

  for (; it!=ie; ++it)
    os << sr->getValue(**it) << (it != it_last ? sep : "");
  os << "\n";
}

// Print statistics for a given round with seperator and newline at the end
void CVStatisticsManager::print_round_with_short_name(
    std::ostream &os, unsigned index, std::string sep) {

  klee::StatisticRecord *sr = statistic_records_[index];

  auto it = begin(statistics_), ie = end(statistics_);
  auto it_last = --end(statistics_); // don't print sep after this element

  for (; it!=ie; ++it)
    os << (*it)->getShortName() << ":" << sr->getValue(**it) << (it != it_last ? sep : "");
  os << "\n";
}

// Print full names of statistics with seperator and newline at the end
void CVStatisticsManager::print_names(std::ostream &os, std::string sep) {
  auto it = begin(statistics_), ie = end(statistics_);
  auto it_last = --end(statistics_); // don't print sep after this element

  for (; it!=ie; ++it)
    os << (*it)->getName() << (it != it_last ? sep : "");
  os << "\n";
}

// Print short names of statistics with seperator and newline at the end
void CVStatisticsManager::print_short_names(std::ostream &os, std::string sep) {
  auto it = begin(statistics_), ie = end(statistics_);
  auto it_last = --end(statistics_); // don't print sep after this element

  for (; it!=ie; ++it)
    os << (*it)->getShortName() << (it != it_last ? sep : "");
  os << "\n";
}

// Print summary (min, max, sum, average) of a given klee::Statistic
void CVStatisticsManager::print_summary(std::ostream &os,
  klee::Statistic* s, std::string sep) {
  unsigned stat_count = statistic_records_.size();

  // Compute stats from contexts
  uint64_t sum = 0, min = 0;
  uint64_t max = statistic_records_[0]->getValue(*s);

  for (auto sr : statistic_records_) {
    uint64_t val = sr->getValue(*s);
    sum += val;
    min = std::min(min, val);
    max = std::max(max, val);
  }

  double average = ((double)sum) / stat_count;
  os << s->getName() << sep << min << sep << max << sep << sum << sep;
  os << std::fixed << std::setprecision(6) << average << "\n";
}

// Round Statistics Summary - (name, minimum, maximum, sum, average)
void CVStatisticsManager::print_all_summary(std::ostream &os, std::string sep) {
  for (auto s : statistics_) {
    print_summary(os, s, sep);
  }
}


// end Statistics.cpp
