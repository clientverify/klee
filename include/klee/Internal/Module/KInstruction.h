//===-- KInstruction.h ------------------------------------------*- C++ -*-===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef KLEE_KINSTRUCTION_H
#define KLEE_KINSTRUCTION_H

#include "klee/Config/Version.h"
#include "llvm/Support/DataTypes.h"
#include "llvm/Support/raw_os_ostream.h"
#include <vector>
#include <iostream>
#include <assert.h>

namespace llvm {
  class Instruction;
}

namespace klee {
  class Executor;
  struct InstructionInfo;
  struct KBasicBlock;
  class KModule;

  /// KInstruction - Intermediate instruction representation used
  /// during execution.
  struct KInstruction {
    llvm::Instruction *inst;    
    const InstructionInfo *info;
    const KBasicBlock *kbb;

    /// Value numbers for each operand. -1 is an invalid value,
    /// otherwise negative numbers are indices (negated and offset by
    /// 2) into the module constant table and positive numbers are
    /// register indices.
    int *operands;
    /// Destination register index.
    unsigned dest;

    /* TODO: add doc... */
    bool isCloned;
    /* points to the original instruction if the instruction is cloned */
    llvm::Instruction *origInst;
    /* relevant only for load instructions */
    bool mayBlock;
    /* relevant only for store instructions */
    bool mayOverride;

  public:
    virtual ~KInstruction(); 

    llvm::Instruction *getOrigInst() {
      if (isCloned) {
        return origInst;
      }
      return inst;
    }
  };

  struct KGEPInstruction : KInstruction {
    /// indices - The list of variable sized adjustments to add to the pointer
    /// operand to execute the instruction. The first element is the operand
    /// index into the GetElementPtr instruction, and the second element is the
    /// element size to multiple that index by.
    std::vector< std::pair<unsigned, uint64_t> > indices;

    /// offset - A constant offset to add to the pointer operand to execute the
    /// instruction.
    uint64_t offset;
  };

  // Utility for printing KInstruction
  inline std::ostream &operator<<(std::ostream &os, const klee::KInstruction &ki) {
    assert(0);
    std::string str;
    llvm::raw_string_ostream ros(str);
    return os << ros.str();
  }
}

#endif

