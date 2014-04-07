//===-- Statistics.cpp ----------------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "klee/Statistics.h"

#include <vector>

using namespace klee;

StatisticManager::StatisticManager()
  : enabled(true),
    globalStats(0),
    indexedStats(0),
    contextStats(0),
    index(0) {
}

StatisticManager::~StatisticManager() {
  if (globalStats) delete[] globalStats;
  if (indexedStats) delete[] indexedStats;
}

void StatisticManager::useIndexedStats(unsigned totalIndices) {  
  if (indexedStats) delete[] indexedStats;
  indexedStats = new StatisticDataType[totalIndices * stats.size()];
  for (int i=0; i< totalIndices * stats.size(); ++i) indexedStats[i] = 0;
}

void StatisticManager::registerStatistic(Statistic &s) {
  if (globalStats) delete[] globalStats;
  s.id = stats.size();
  stats.push_back(&s);
  globalStats = new StatisticDataType[stats.size()];
  for (int i=0; i< stats.size(); ++i) globalStats[i] = 0;
}

int StatisticManager::getStatisticID(const std::string &name) const {
  for (unsigned i=0; i<stats.size(); i++)
    if (stats[i]->getName() == name)
      return i;
  return -1;
}

Statistic *StatisticManager::getStatisticByName(const std::string &name) const {
  for (unsigned i=0; i<stats.size(); i++)
    if (stats[i]->getName() == name)
      return stats[i];
  return 0;
}

StatisticManager *klee::theStatisticManager = 0;

static StatisticManager &getStatisticManager() {
  static StatisticManager sm;
  theStatisticManager = &sm;
  return sm;
}

/* *** */

Statistic::Statistic(const std::string &_name, 
                     const std::string &_shortName) 
  : name(_name), 
    shortName(_shortName) {
  getStatisticManager().registerStatistic(*this);
}

Statistic::~Statistic() {
}

Statistic &Statistic::operator +=(const uint64_t addend) {
  theStatisticManager->incrementStatistic(*this, addend);
  return *this;
}

uint64_t Statistic::getValue() const {
  return theStatisticManager->getValue(*this);
}
