//===-- SearcherTest.cpp --------------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "klee/Internal/Module/KModule.h"
#include "klee/ExecutionState.h"

#include "llvm/IR/IRBuilder.h"
#include "llvm/ADT/OwningPtr.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/MDBuilder.h"
#include "llvm/IR/Module.h"
#include "gtest/gtest.h"

#include <iostream>
#include <thread>

#include "../../lib/Core/Searcher.h"

using namespace klee;
using namespace llvm;

namespace {

//////////////////////////////////////////////////////////////////////////////////
//
// Searcher Test Strategy
//
// Goals: check for correctness and test performance of ParallelSearcher and
// CVSearcher implementations
//
// Testing CVSearcher: need "fake states" and trigger execution events
//
//////////////////////////////////////////////////////////////////////////////////

class SearcherTest : public ::testing::Test {
 public:

  void AddStates(Searcher *s) {
    for (int i=0; i<40; ++i)
      s->addState(new ExecutionState(KF));
  }

 protected:

  virtual void SetUp() {
    M.reset(new Module("MyModule", Ctx));
    FunctionType *FTy = FunctionType::get(Type::getVoidTy(Ctx),
                                          /*isVarArg=*/false);
    F = Function::Create(FTy, Function::ExternalLinkage, "", M.get());
    BB = BasicBlock::Create(Ctx, "", F);
    GV = new GlobalVariable(*M, Type::getFloatTy(Ctx), true,
                            GlobalValue::ExternalLinkage, 0);
    KM = new KModule(M.get());
    KF = new KFunction(F, KM);
    for (int i=0; i<40; ++i)
      states.push_back(new ExecutionState(KF));
  }

  virtual void TearDown() {
    BB = 0;
    M.reset();
  }

  LLVMContext Ctx;
  OwningPtr<Module> M;
  Function *F;
  BasicBlock *BB;
  GlobalVariable *GV;

  KFunction* KF;
  KModule* KM;
  std::vector<ExecutionState*> states;
};

TEST_F(SearcherTest, BasicConstruction) {
  Searcher* s = new DFSSearcher();
  ASSERT_TRUE(s);
  for (auto es : this->states) {
    s->addState(es);
  }
  delete s;
}

void doStateAdd(klee::Searcher *s, klee::ExecutionState* es) {
  s->addState(es);
}

TEST_F(SearcherTest, BasicConstructionParallel) {
  Searcher* s = new ParallelDFSSearcher();
  ASSERT_TRUE(s);
  std::vector<std::thread*> threads;

  for (auto es : this->states) {
    std::thread *t = new std::thread(&SearcherTest::AddStates, this, s);
    threads.push_back(t);
  }

  for (auto t : threads)
    t->join();

  delete s;
}

}


