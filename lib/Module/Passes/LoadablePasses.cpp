//===-- LoadablePasses.cpp ------------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "../Passes.h"

static llvm::RegisterPass<klee::PhiCleanerPass> RP_phicleaner(
    "phicleaner", "Phi Cleaner Pass (klee)", false, false);

static llvm::RegisterPass<klee::IntrinsicCleanerPass> RP_intrinsiccleaner(
    "intrinsiccleaner", "Intrinisc Cleaner Pass (klee)", false, false);

// hack so we can load this code as a module, while not including
// commandline flags that conflict with builtins in opt
#include "../PhiCleaner.cpp"
#include "../IntrinsicCleaner.cpp"
