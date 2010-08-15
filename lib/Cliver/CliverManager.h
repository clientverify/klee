//===-- CliverManager.h ---------------------------------*- C++ -*-===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the CliverManager interface.
//
//===----------------------------------------------------------------------===//

#ifndef CLIVERMANAGER_H
#define CLIVERMANAGER_H
#include <map>
#include <vector>
#include <string>

namespace llvm {
  class Function;
}

namespace klee {
  class Executor;
  class Expr;
  class ExecutionState;
  class KInstruction;
  template<typename T> class ref;
  
  class CliverManager {
  public:
    class Executor &executor;

  public:
    CliverManager(Executor &_executor);

  };
} // End klee namespace

#endif //CLIVERMANAGER_H
