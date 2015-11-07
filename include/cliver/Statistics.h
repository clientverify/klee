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

#include "cliver/CliverStats.h"

#include <map>
#include <vector>

namespace cliver {
class SearcherStage;
}

class CVStatisticsManager {
public:
  CVStatisticsManager();
  void initialize();
  void set_context(unsigned index, cliver::SearcherStage *stage);
  uint64_t get_context_statistic_value(unsigned index,
                                       const klee::Statistic &s);

  void print_statistic_record(std::ostream &os, klee::StatisticRecord *sr,
                              std::string sep, bool shortName = false);
  void print_round(std::ostream &os, unsigned index, std::string sep);
  void print_stage(std::ostream &os, cliver::SearcherStage *stage,
                   std::string sep);
  void print_round_with_short_name(std::ostream &os, unsigned index,
                                   std::string sep);
  void print_all_rounds(std::ostream &os, std::string sep);
  void print_all_stages(std::ostream &os, std::string sep);
  void print_names(std::ostream &os, std::string sep);
  void print_short_names(std::ostream &os, std::string sep);
  void print_all_summary(std::ostream &os, std::string sep);
  void print_summary(std::ostream &os, klee::Statistic *s, std::string sep);
  void update_context_timers();

  uint64_t get_round_statistic(unsigned index, std::string stat_name);
  uint64_t get_stage_statistic(cliver::SearcherStage *stage,
                               std::string stat_name);

private:
  // All of the klee::Statistic types to be printed (cliver and klee)
  std::vector<klee::Statistic *> statistics_;

  // StatisticRecord for each message index
  std::vector<klee::StatisticRecord *> statistic_records_;

  // StatisticRecord for each SearcherStage
  std::map<cliver::SearcherStage *, klee::StatisticRecord *>
  stage_statistic_records_;

  // Process time usage stats last time we switch cliver context
  llvm::sys::TimeValue context_now_time_;
  llvm::sys::TimeValue context_user_time_;
  llvm::sys::TimeValue context_sys_time_;
};

#endif // CLIVER_STATISTICS_H
