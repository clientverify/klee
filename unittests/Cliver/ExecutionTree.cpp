//===-- ExecutionTree.cpp ------------------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include <iostream>
#include "gtest/gtest.h"

#include "klee/Expr.h"
#include "cliver/ClientVerifier.h"
#include "cliver/ExecutionTree.h"
#include "cliver/EditDistance.h"
#include "cliver/CVStream.h"

#include "llvm/Analysis/Trace.h"
#include "llvm/BasicBlock.h"
#include "llvm/LLVMContext.h"

#include <string>
#include <iostream>

using namespace klee;
using namespace cliver;
using namespace std;

namespace {

////////////////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////////////////

TEST(ExecutionTreeTest, ExecutionTrace1) {
  ExecutionTrace etrace, etrace_copy;

  ExecutionTrace::ID ids[] = { 10, 20, 30, 40, 50, 60, 70, 80, 90, 100 };

  for(int i=0;i<10;i++) {
    etrace.push_back(ids[i]);
  }
  etrace_copy.push_back(etrace);

  EXPECT_EQ(10, etrace.size());
  EXPECT_EQ(etrace_copy.size(), etrace.size());
  EXPECT_EQ(etrace_copy, etrace);

}

////////////////////////////////////////////////////////////////////////////////

}
