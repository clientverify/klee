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
#include "klee/util/Thread.h"
#include "klee/Internal/ADT/RNG.h"

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
#include "../../lib/Core/ParallelSearcher.h"

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
      s->addState(GetNewState());
  }

  enum SimulationEvent {
    INSTRUCTION = 0,
    BRANCH = 1
  };

  int checkCDF(double lookup, double cdf[], int len) {
    assert(lookup < cdf[len-1] || lookup < cdf[0]);
    for (int i=0; i<len; ++i) {
      if (lookup < cdf[i]) {
        return i;
      }
    }
  }

  ExecutionState* GetNewState() {
    return new ExecutionState(KF);
  }

  void SimulateStateForks(Searcher *s) {
    if (!theRNG.get()) {
      theRNG.reset(new RNG());
      theRNG->seed(std::hash<std::thread::id>()(std::this_thread::get_id()));
    }

    std::set<ExecutionState*> added_states;
    std::set<ExecutionState*> removed_states;

    ExecutionState* state = GetNewState();
    s->update(state, added_states, removed_states);

    int instruction_count = 0;
    int add_count = 0;
    int remove_count = 0;
    while (instruction_count < 100000) {
      SimulationEvent ev = (SimulationEvent)(theRNG->getInt32() % 2);
      instruction_count++;
      double branchCDF[] = {0.7,0.82,0.9,1.0};

      switch (ev) {
        case INSTRUCTION:
          {
            // execute instruction
            s->update(state, added_states, removed_states);
          }
          break;
        case BRANCH:
          {
            // Branch Types:
            // 0: Branch is concrete
            // 1: Branch is symbolic (add state)
            // 2: Branch is not sat (remove state)
            // 3: Branch is crazy! (add and reemove state)
            int branch_type = checkCDF(theRNG->getDouble(),
                                      branchCDF, 4);
            switch (branch_type) {
              case 0:
                s->update(state, added_states, removed_states);
                break;
              case 1:
                add_count++;
                added_states.insert(GetNewState());
                s->update(state, added_states, removed_states);
                added_states.clear();
                break;
              case 2:
                remove_count++;
                removed_states.insert(state);
                s->update(state, added_states, removed_states);
                removed_states.clear();
                break;
              case 3:
                add_count++;
                remove_count++;
                removed_states.insert(state);
                added_states.insert(GetNewState());
                s->update(state, added_states, removed_states);
                removed_states.clear();
                added_states.clear();
                break;
            }
          }
          break;
      }
    }
    //std::cout << "Add: " << add_count << ", Remove: " << remove_count <<"\n";
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
  ThreadSpecificPointer<RNG>::type theRNG;
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

TEST_F(SearcherTest, SimulateStateForksParallelBFS) {
  Searcher* s = new ParallelSearcher(new BFSSearcher());
  ASSERT_TRUE(s);

  ThreadGroup threadGroup;
  //for (auto es : this->states) {
  for (int i=0; i<5; ++i) {
    threadGroup.add_thread(new Thread(&SearcherTest::SimulateStateForks,
                           this, s));
  }
  threadGroup.join_all();

  delete s;
}

TEST_F(SearcherTest, SimulateStateForksParallelDFS) {
  Searcher* s = new ParallelDFSSearcher();
  ASSERT_TRUE(s);

  ThreadGroup threadGroup;
  //for (auto es : this->states) {
  for (int i=0; i<5; ++i) {
    threadGroup.add_thread(new Thread(&SearcherTest::SimulateStateForks,
                           this, s));
  }
  threadGroup.join_all();

  delete s;
}



//TEST_F(SearcherTest, SimulateStateForksParallel) {
//  Searcher* s = new ParallelDFSSearcher();
//  ASSERT_TRUE(s);
//  std::vector<std::thread*> threads;
//
//  for (auto es : this->states) {
//    std::thread *t = new std::thread(&SearcherTest::SimulateStateForks,
//                                     this, s);
//    threads.push_back(t);
//  }
//
//  for (auto t : threads)
//    t->join();
//
//  delete s;
//}

}


