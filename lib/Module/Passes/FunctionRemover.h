//===-- FunctionRemover.h ----------------------------------------*- C++ -*-===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef KLEE_FUNCTION_REMOVER_H
#define KLEE_FUNCTION_REMOVER_H

#include "klee/Config/Version.h"

#include "llvm/IR/Constants.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/CallSite.h"
#include "llvm/Pass.h"
#include "llvm/CodeGen/IntrinsicLowering.h"
#include "llvm/Support/raw_ostream.h"

namespace llvm {
  class Function;
  class Instruction;
  class Module;
  class DataLayout;
  class TargetLowering;
  class Type;
}

namespace klee {
 struct FunctionRemover : public llvm::BasicBlockPass {
    static char ID; // Pass identification, replacement for typeid
    FunctionRemover() : llvm::BasicBlockPass(ID) {
      //initializeDeadInstRemoverPass(*PassRegistry::getPassRegistry());
    }
    virtual bool runOnBasicBlock(llvm::BasicBlock &BB) {
      using namespace llvm;
      bool Changed = false;
      for (BasicBlock::iterator DI = BB.begin(); DI != BB.end(); ) {
        Instruction *Inst = DI++;
        if (CallInst *CI = dyn_cast<CallInst>(Inst)) {
          CallSite cs(CI);
          if (Function *f = dyn_cast<Function>(cs.getCalledValue())) {
            if (f->getName() == "printf") {
              errs() << "Call to printf!\n";
              Inst->eraseFromParent();
              Changed = true;
            }
          }
          //if (Function *f = dyn_cast<Function>(cs.getCalledValue())) {
          //  if (f->getName().str()[0] == 'X' && f->getName().str().substr(0,6)!= "Xpilot") {
          //    errs() << "XFunction: " << f->getName().str() << "\n";
          //    f->addAttribute(AttributeSet::FunctionIndex, Attribute::ReadOnly);
          //    Inst->eraseFromParent();
          //    Changed = true;

          //    Function *F = cast<Function>(
          //      M.getOrInsertFunction(
          //        "abort", Type::getVoidTy(getGlobalContext()), NULL));
    

          //  }
          //}

        }
        //if (isInstructionTriviallyDead(Inst, TLI)) {
        //  Inst->eraseFromParent();
        //  Changed = true;
        //  ++DIEEliminated;
        //}
      }
      return Changed;
    }

    //virtual void getAnalysisUsage(AnalysisUsage &AU) const {
    //  AU.setPreservesCFG();
    //}
  };
}

#endif
