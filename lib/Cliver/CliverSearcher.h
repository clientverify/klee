//===-- CliverSearcher.h -----------------------------------------*- C++ -*-===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the clvr::CliverSearcher interface.
//
// CliverSearcher implements the klee::Searcher interface, which is used to
// define different strategies for determining which ExecutionState is to be
// executed next. The goals of CliverSearcher are a modular design, utilizing
// one or more ExecutionStatePruner and ExecutionStateMerger classes.
//
//===----------------------------------------------------------------------===//

#ifndef KLEE_CLVR_CLIVERSEARCHER_H
#define KLEE_CLVR_CLIVERSEARCHER_H

namespace klee {
namespace clvr {
	class CliverSearcher : public Searcher {
		public:
			CliverSearcher();
		private:
	};

} // end namespace clvr 
} // end namespace klee
#endif // KLEE_CLVR_CLIVERSEARCHER_H
