//===-- CoreStats.cpp -----------------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "CoreStats.h"

using namespace klee;

/* NUKLEAR KLEE begin */
Statistic stats::nuklearPrunes("NuklearPrunes", "nPru");
Statistic stats::nuklearMerges("NuklearMerges", "nMrg");
Statistic stats::nuklearPreMergeStates("NuklearPreMergeStates", "nPMS");
Statistic stats::nuklearStateErases("NuklearStateErases", "nSErs");
Statistic stats::nuklearRoundStates("NuklearRoundStates", "nRSts");
Statistic stats::nuklearExecuteTime("NuklearExecuteTime", "nETime");
Statistic stats::nuklearMergeTime("NuklearMergeTime", "nMTime");
Statistic stats::nuklearPruneTime("NuklearPruneTime", "nPTime");
Statistic stats::nuklearRoundTime("NuklearRoundTime", "nRTime");
Statistic stats::nuklearRoundTimeReal("NuklearRoundTimeReal", "nRTimeR");

Statistic stats::nuklearPruneTime_stack("NuklearPruneTime_stack", "nPTime_stk");
Statistic stats::nuklearPruneTime_addressSpace("NuklearPruneTime_addressSpace", "nPTime_addrSp");
Statistic stats::nuklearPruneTime_symprune("NuklearPruneTime_symprune", "nPTime_spr");
Statistic stats::nuklearPruneTime_constraintprune("NuklearPruneTime_constraintprune", "nPTime_cpr");

Statistic stats::nuklearDigestTime("NuklearDigestTime", "nDgTime");
Statistic stats::nuklearDigestTimeEVP("NuklearDigestTimeEVP", "nDgTimeEVP");
/* NUKLEAR KLEE end */

Statistic stats::allocations("Allocations", "Alloc");
Statistic stats::coveredInstructions("CoveredInstructions", "Icov");
Statistic stats::falseBranches("FalseBranches", "Bf");
Statistic stats::forkTime("ForkTime", "Ftime");
Statistic stats::forks("Forks", "Forks");
Statistic stats::instructionRealTime("InstructionRealTimes", "Ireal");
Statistic stats::instructionTime("InstructionTimes", "Itime");
Statistic stats::instructions("Instructions", "I");
Statistic stats::minDistToReturn("MinDistToReturn", "Rdist");
Statistic stats::minDistToUncovered("MinDistToUncovered", "UCdist");
Statistic stats::reachableUncovered("ReachableUncovered", "IuncovReach");
Statistic stats::resolveTime("ResolveTime", "Rtime");
Statistic stats::solverTime("SolverTime", "Stime");
Statistic stats::states("States", "States");
Statistic stats::trueBranches("TrueBranches", "Bt");
Statistic stats::uncoveredInstructions("UncoveredInstructions", "Iuncov");
