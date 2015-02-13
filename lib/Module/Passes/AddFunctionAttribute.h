//===-- FunctionRemover.h ----------------------------------------*- C++ -*-===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef KLEE_ADD_FUNCTION_ATTRIBUTE_H
#define KLEE_ADD_FUNCTION_ATTRIBUTE_H

#include "klee/Config/Version.h"

#include "llvm/IR/Constants.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/CallSite.h"
#include "llvm/Pass.h"
#include "llvm/CodeGen/IntrinsicLowering.h"
#include "llvm/Support/raw_ostream.h"

#include <set>

namespace llvm {
  class Function;
  class Instruction;
  class Module;
  class DataLayout;
  class TargetLowering;
  class Type;
}

namespace klee {
class AddFunctionAttribute : public llvm::ModulePass {
  std::map<std::string, std::vector<llvm::Attribute> > Attributes;

public:
  static char ID;
  AddFunctionAttribute();
  virtual bool runOnModule(llvm::Module &M);
};
}
 

#endif //KLEE_ADD_FUNCTION_ATTRIBUTE_H
