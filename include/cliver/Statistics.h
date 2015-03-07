//===-- Statistics.h --- ----------------------------------------*- C++ -*-===//
//
// <insert license>
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//
#ifndef CLIVER_STATISTICS_H
#define CLIVER_STATISTICS_H

#include "klee/Statistic.h"
#include "klee/Statistics.h"

#include "llvm/Support/Process.h"

namespace stats {

#define XSTAT(X,NAME,SHORTNAME) extern klee::Statistic X;
#include "cliver/stats.inc"
#undef XSTAT

}

class CVStatisticsManager {
 public:
  CVStatisticsManager();
  void initialize();
  void set_context(unsigned index);
  void print_round(std::ostream &os, unsigned index, std::string sep);
  void print_round_with_short_name(std::ostream &os, unsigned index, std::string sep);
  void print_all_rounds(std::ostream &os, std::string sep);
  void print_names(std::ostream &os, std::string sep);
  void print_short_names(std::ostream &os, std::string sep);
  void print_all_summary(std::ostream &os, std::string sep);
  void print_summary(std::ostream &os, klee::Statistic* s, std::string sep);
  void update_context_timers();

 private:

  // All of the klee::Statistic types to be printed (cliver and klee)
  std::vector<klee::Statistic*> statistics_;

  // StatisticRecord for each network round
  std::vector<klee::StatisticRecord*> statistic_records_;

  // Process time usage stats last time we switch cliver context
  llvm::sys::TimeValue context_now_time_;
  llvm::sys::TimeValue context_user_time_;
  llvm::sys::TimeValue context_sys_time_;
};

#endif // CLIVER_STATISTICS_H
