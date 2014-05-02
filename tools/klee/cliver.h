//===-- ClientVerifier.h ----------------------------------------*- C++ -*-===//
//
// <insert license>
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//
#ifndef CLIVER_H
#define CLIVER_H

#include "llvm/Support/CommandLine.h"

namespace llvm {
// Command line options defined in lib/Module/Optimize.cpp
extern cl::opt<bool> DontVerify;
extern cl::opt<bool> DisableInline;
extern cl::opt<bool> DisableOptimizations;
extern cl::opt<bool> DisableInternalize;
extern cl::opt<bool> Strip;
}

namespace klee {

enum SwitchImplType {
  eSwitchTypeSimple,
  eSwitchTypeLLVM,
  eSwitchTypeInternal
};

// Command line options defined in lib/Basic/CmdLineOptions.cpp
extern llvm::cl::opt<bool> UseFastCexSolver;
extern llvm::cl::opt<bool> UseCexCache;
extern llvm::cl::opt<bool> UseCache;
extern llvm::cl::opt<bool> UseCanonicalization;
extern llvm::cl::opt<bool> UseIndependentSolver;
extern llvm::cl::opt<bool> DebugValidateSolver;
extern llvm::cl::opt<int> MinQueryTimeToLog;
extern llvm::cl::opt<double> MaxCoreSolverTime;
extern llvm::cl::opt<bool> UseForkedCoreSolver;
extern llvm::cl::opt<bool> CoreSolverOptimizeDivides;

// Command line options defined in lib/Core/Executor.cpp
extern llvm::cl::opt<bool> DumpStatesOnHalt;
extern llvm::cl::opt<bool> NoPreferCex;
extern llvm::cl::opt<bool> UseAsmAddresses;
extern llvm::cl::opt<bool> RandomizeFork;
extern llvm::cl::opt<bool> AllowExternalSymCalls;
extern llvm::cl::opt<bool> DebugPrintInstructions;
extern llvm::cl::opt<bool> DebugCheckForImpliedValues;
extern llvm::cl::opt<bool> SimplifySymIndices;
extern llvm::cl::opt<unsigned> MaxSymArraySize;
extern llvm::cl::opt<bool> SuppressExternalWarnings;
extern llvm::cl::opt<bool> AllExternalWarnings;
extern llvm::cl::opt<bool> OnlyOutputStatesCoveringNew;
extern llvm::cl::opt<bool> AlwaysOutputSeeds;
extern llvm::cl::opt<bool> EmitAllErrors;
extern llvm::cl::opt<bool> UseQueryPCLog;
extern llvm::cl::opt<bool> UseSTPQueryPCLog;
extern llvm::cl::opt<bool> NoExternals;
extern llvm::cl::opt<bool> OnlyReplaySeeds;
extern llvm::cl::opt<bool> OnlySeed;
extern llvm::cl::opt<bool> AllowSeedExtension;
extern llvm::cl::opt<bool> ZeroSeedExtension;
extern llvm::cl::opt<bool> AllowSeedTruncation;
extern llvm::cl::opt<bool> NamedSeedMatching;
extern llvm::cl::opt<double> MaxStaticForkPct;
extern llvm::cl::opt<double> MaxStaticSolvePct;
extern llvm::cl::opt<double> MaxStaticCPForkPct;
extern llvm::cl::opt<double> MaxStaticCPSolvePct;
extern llvm::cl::opt<double> MaxInstructionTime;
extern llvm::cl::opt<double> SeedTime;
extern llvm::cl::opt<double> MaxSTPTime;
extern llvm::cl::opt<unsigned int> StopAfterNInstructions;
extern llvm::cl::opt<unsigned> MaxForks;
extern llvm::cl::opt<unsigned> MaxDepth;
extern llvm::cl::opt<unsigned> MaxMemory;
extern llvm::cl::opt<bool> MaxMemoryInhibit;

// Command line options defined in tools/klee/main.cpp
extern llvm::cl::opt<LibcType> Libc;
extern llvm::cl::opt<bool> WithPOSIXRuntime;
extern llvm::cl::opt<bool> OptimizeModule;
extern llvm::cl::opt<bool> CheckDivZero;
extern llvm::cl::opt<bool> CheckOvershift;

// Command line options defined in lib/Core/Searcher.cpp
extern llvm::cl::opt<bool> UseMerge;
extern llvm::cl::opt<bool> UseBumpMerge;
extern llvm::cl::opt<bool> UseIterativeDeepeningTimeSearch;
extern llvm::cl::opt<bool> UseBatchingSearch;
extern llvm::cl::opt<SwitchImplType> SwitchType;

// Command line options defined in lib/Core/StatsTracker.cpp
extern llvm::cl::opt<bool> OutputIStats;
extern llvm::cl::opt<bool> UseCallPaths;

// Command line options defined in lib/Core/UserSearcher.cpp
extern llvm::cl::opt<unsigned> UseThreads;

bool containsArg(std::string arg, std::vector<std::string> &args) {
  for (int i=0; i<args.size(); ++i)
    if (args[i].find(arg) != std::string::npos)
      return true;
  return false;
}

void processKleeArgumentsForCliver(std::vector<std::string> &args) {
#define SET_DEFAULT_OPT(name, val) \
	if (name != val && !containsArg(#name, args)) { \
    name=val; klee_message("CV: Setting %s to %s", #name, #val);}
	//SET_DEFAULT_OPT(WithPOSIXRuntime,true);
	//SET_DEFAULT_OPT(Libc,UcLibc);
	SET_DEFAULT_OPT(CheckDivZero,false);
	SET_DEFAULT_OPT(CheckOvershift,false);
	SET_DEFAULT_OPT(OptimizeModule,true);
	SET_DEFAULT_OPT(UseForkedCoreSolver,false);
	SET_DEFAULT_OPT(SwitchType,eSwitchTypeSimple);
	SET_DEFAULT_OPT(UseCanonicalization,true);
	SET_DEFAULT_OPT(UseCallPaths,false);
	SET_DEFAULT_OPT(OutputIStats,false);
	SET_DEFAULT_OPT(llvm::DisableInline,true);
	SET_DEFAULT_OPT(llvm::DisableInternalize,true);
#undef SET_DEFAULT_OPT

#define INVALID_CL_OPT(name, val) \
	if (name != val) {klee_error("CV: Unsupported command line option: %s", #name);}
	INVALID_CL_OPT(ZeroSeedExtension,false);
	INVALID_CL_OPT(AllowSeedExtension,false);
	INVALID_CL_OPT(AlwaysOutputSeeds,true);
	INVALID_CL_OPT(OnlyReplaySeeds,false);
	INVALID_CL_OPT(OnlySeed,false);
	INVALID_CL_OPT(NamedSeedMatching,false);
	INVALID_CL_OPT(MaxDepth,false);
	INVALID_CL_OPT(UseMerge,false);
	INVALID_CL_OPT(UseBumpMerge,false);
	INVALID_CL_OPT(UseIterativeDeepeningTimeSearch,false);
	INVALID_CL_OPT(UseBatchingSearch,false);
	INVALID_CL_OPT(MaxCoreSolverTime,0.0);
	INVALID_CL_OPT(SwitchType,eSwitchTypeSimple);
#undef INVALID_CL_OPT
}

////////////////////////////////////////////////////////////////////////////////

} // end namespace klee

#endif // CLIVER_H
