//===-- UserSearcher.cpp --------------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "Common.h"

#include "UserSearcher.h"

#include "Searcher.h"
#include "ParallelSearcher.h"
#include "Executor.h"

#include "llvm/Support/CommandLine.h"

using namespace llvm;
using namespace klee;

namespace klee {
  cl::list<Searcher::CoreSearchType>
  CoreSearch("search", cl::desc("Specify the search heuristic (default=random-path interleaved with nurs:covnew)"),
	     cl::values(clEnumValN(Searcher::DFS, "dfs", "use Depth First Search (DFS)"),
			clEnumValN(Searcher::BFS, "bfs", "use Breadth First Search (BFS)"),
			clEnumValN(Searcher::RandomState, "random-state", "randomly select a state to explore"),
			clEnumValN(Searcher::RandomPath, "random-path", "use Random Path Selection (see OSDI'08 paper)"),
			clEnumValN(Searcher::NURS_CovNew, "nurs:covnew", "use Non Uniform Random Search (NURS) with Coverage-New"),
			clEnumValN(Searcher::NURS_MD2U, "nurs:md2u", "use NURS with Min-Dist-to-Uncovered"),
			clEnumValN(Searcher::NURS_Depth, "nurs:depth", "use NURS with 2^depth"),
			clEnumValN(Searcher::NURS_ICnt, "nurs:icnt", "use NURS with Instr-Count"),
			clEnumValN(Searcher::NURS_CPICnt, "nurs:cpicnt", "use NURS with CallPath-Instr-Count"),
			clEnumValN(Searcher::NURS_QC, "nurs:qc", "use NURS with Query-Cost"),
			clEnumValEnd));

  cl::opt<bool>
  UseIterativeDeepeningTimeSearch("use-iterative-deepening-time-search", 
                                    cl::desc("(experimental)"));

  cl::opt<bool>
  UseBatchingSearch("use-batching-search", 
		    cl::desc("Use batching searcher (keep running selected state for N instructions/time, see --batch-instructions and --batch-time)"),
		    cl::init(false));

  cl::opt<unsigned>
  BatchInstructions("batch-instructions",
                    cl::desc("Number of instructions to batch when using --use-batching-search"),
                    cl::init(10000));
  
  cl::opt<double>
  BatchTime("batch-time",
            cl::desc("Amount of time to batch when using --use-batching-search"),
            cl::init(5.0));


  cl::opt<bool>
  UseMerge("use-merge", 
           cl::desc("Enable support for klee_merge() (experimental)"));
 
  cl::opt<bool>
  UseBumpMerge("use-bump-merge", 
           cl::desc("Enable support for klee_merge() (extra experimental)"));

  cl::opt<unsigned>
  UseThreads("use-threads",
           cl::desc("Use multiple threads"),
           cl::init(1));

  extern cl::opt<bool> UseProcessTree;
}


bool klee::userSearcherRequiresMD2U() {
  return (std::find(CoreSearch.begin(), CoreSearch.end(), Searcher::NURS_MD2U) != CoreSearch.end() ||
	  std::find(CoreSearch.begin(), CoreSearch.end(), Searcher::NURS_CovNew) != CoreSearch.end() ||
	  std::find(CoreSearch.begin(), CoreSearch.end(), Searcher::NURS_ICnt) != CoreSearch.end() ||
	  std::find(CoreSearch.begin(), CoreSearch.end(), Searcher::NURS_CPICnt) != CoreSearch.end() ||
	  std::find(CoreSearch.begin(), CoreSearch.end(), Searcher::NURS_QC) != CoreSearch.end());
}


Searcher *getNewSearcher(Searcher::CoreSearchType type, Executor &executor) {
  Searcher *searcher = NULL;
  switch (type) {
  case Searcher::DFS: searcher = new DFSSearcher(); break;
  case Searcher::BFS: searcher = new BFSSearcher(); break;
  case Searcher::RandomState: searcher = new RandomSearcher(); break;
  case Searcher::RandomPath: searcher = new RandomPathSearcher(executor); break;
  case Searcher::NURS_CovNew: searcher = new WeightedRandomSearcher(WeightedRandomSearcher::CoveringNew); break;
  case Searcher::NURS_MD2U: searcher = new WeightedRandomSearcher(WeightedRandomSearcher::MinDistToUncovered); break;
  case Searcher::NURS_Depth: searcher = new WeightedRandomSearcher(WeightedRandomSearcher::Depth); break;
  case Searcher::NURS_ICnt: searcher = new WeightedRandomSearcher(WeightedRandomSearcher::InstCount); break;
  case Searcher::NURS_CPICnt: searcher = new WeightedRandomSearcher(WeightedRandomSearcher::CPInstCount); break;
  case Searcher::NURS_QC: searcher = new WeightedRandomSearcher(WeightedRandomSearcher::QueryCost); break;
  }

  return searcher;
}

Searcher *getNewParallelSearcher(Searcher::CoreSearchType type, Executor &executor) {
  Searcher *searcher = NULL;
  switch (type) {
  case Searcher::DFS: searcher = new ParallelDFSSearcher(); break;
  case Searcher::BFS: searcher = new ParallelSearcher(new BFSSearcher()); break;
  case Searcher::RandomState: searcher = new ParallelSearcher(new RandomSearcher()); break;
  case Searcher::RandomPath: searcher = new ParallelSearcher(new RandomPathSearcher(executor)); break;
  case Searcher::NURS_CovNew: searcher = new ParallelWeightedRandomSearcher(executor, ParallelWeightedRandomSearcher::CoveringNew); break;
  case Searcher::NURS_MD2U: searcher = new ParallelWeightedRandomSearcher(executor, ParallelWeightedRandomSearcher::MinDistToUncovered); break;
  case Searcher::NURS_Depth: searcher = new ParallelWeightedRandomSearcher(executor, ParallelWeightedRandomSearcher::Depth); break;
  case Searcher::NURS_ICnt: searcher = new ParallelWeightedRandomSearcher(executor, ParallelWeightedRandomSearcher::InstCount); break;
  case Searcher::NURS_CPICnt: searcher = new ParallelWeightedRandomSearcher(executor, ParallelWeightedRandomSearcher::CPInstCount); break;
  case Searcher::NURS_QC: searcher = new ParallelWeightedRandomSearcher(executor, ParallelWeightedRandomSearcher::QueryCost); break;
  }

  return searcher;
}

Searcher *klee::constructUserSearcher(Executor &executor) {

  // Check for invalid searcher configurations
  if (UseMerge || UseBumpMerge) {
    if (UseThreads > 1) {
      llvm::raw_ostream &os = executor.getHandler().getInfoStream();
      os << "Invalid configuration: multiple threads not supported this configuration. ";
      os << "Setting thread count to 1\n";
      UseThreads = 1;
    }
  }

  if (!UseProcessTree && 
      std::find(CoreSearch.begin(), CoreSearch.end(), Searcher::RandomPath) 
        != CoreSearch.end()) {
    llvm::raw_ostream &os = executor.getHandler().getInfoStream();
    os << "Invalid configuration: -random-path=true requires -process-tree=true\n";
    return NULL;
  }

  // default values
  if (CoreSearch.size() == 0) {
    CoreSearch.push_back(Searcher::RandomPath);
    CoreSearch.push_back(Searcher::NURS_CovNew);
  }

  Searcher *searcher = NULL;
  if (UseThreads > 1 && CoreSearch.size() == 1)
    searcher = getNewParallelSearcher(CoreSearch[0], executor);
  else
    searcher = getNewSearcher(CoreSearch[0], executor);
  
  if (CoreSearch.size() > 1) {
    std::vector<Searcher *> s;
    s.push_back(searcher);

    for (unsigned i=1; i<CoreSearch.size(); i++)
      s.push_back(getNewSearcher(CoreSearch[i], executor));
    
    searcher = new InterleavedSearcher(s);
  }

  if (UseBatchingSearch) {
    if (UseThreads > 1) {
      searcher = new ParallelBatchingSearcher(searcher, BatchTime, BatchInstructions);
    } else {
      searcher = new BatchingSearcher(searcher, BatchTime, BatchInstructions);
    }
  }

  // merge support is experimental
  if (UseMerge) {
    assert(!UseBumpMerge);
    assert(std::find(CoreSearch.begin(), CoreSearch.end(), Searcher::RandomPath) == CoreSearch.end()); // XXX: needs further debugging: test/Features/Searchers.c fails with this searcher
    searcher = new MergingSearcher(executor, searcher);
  } else if (UseBumpMerge) {
    searcher = new BumpMergingSearcher(executor, searcher);
  }
  
  if (UseIterativeDeepeningTimeSearch) {
    searcher = new IterativeDeepeningTimeSearcher(searcher);
  }

  if (UseThreads > 1 && CoreSearch.size() > 1) {
    searcher = new ParallelSearcher(searcher);
  }

  llvm::raw_ostream &os = executor.getHandler().getInfoStream();

  os << "BEGIN searcher description\n";
  searcher->printName(os);
  os << "END searcher description\n";

  return searcher;
}
