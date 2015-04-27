#ifndef CLIVER_STATS_H
#define CLIVER_STATS_H

#include "klee/Statistic.h"

namespace stats {
#define XSTAT(X,NAME,SHORTNAME) extern klee::Statistic X;
#include "cliver/stats.inc"
#undef XSTAT
}

#endif // CLIVER_STATS_H
