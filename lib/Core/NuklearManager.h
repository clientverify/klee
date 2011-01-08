/* NUKLEAR KLEE begin (ENTIRE FILE) */
//===-- NuklearManager.h ----------------------------------------*- C++ -*-===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef KLEE_NUKLEARMANAGER_H
#define KLEE_NUKLEARMANAGER_H

#include "klee/Statistics.h"
#include "klee/ExecutionState.h"
#include "klee/Interpreter.h"
#include "klee/NuklearSocket.h"
#include "klee/Internal/Module/Cell.h"
#include "klee/Internal/Module/KInstruction.h"
#include "klee/Internal/Module/KModule.h"
#include "llvm/Support/CallSite.h"
#include "Executor.h"
#include <vector>
#include <deque>
#include <string>
#include <map>
#include <set>

namespace klee {

enum NuklearEventType{
  SocketWriteEvent, SocketReadEvent, SuccessEvent, FailureEvent 
};

struct SymbolicInstance {
  std::vector< const Array* > arrays; 
  std::vector< std::string > names; 
  std::vector< ref<Expr> > exprs; 
  std::vector< MemoryObject* > memoryObjects; 
  std::vector< ObjectState* > objectStates; 
  std::map< unsigned, unsigned > users;
  std::string baseName;
  int roundNumber;

  SymbolicInstance(std::string name, int round) 
    : baseName(name), roundNumber(round) {}
  
  ~SymbolicInstance() {
    for (std::vector< const Array* >::iterator 
         it=arrays.begin(), ie=arrays.end(); ie!=it; ++it) {
      delete *it;
    }
    for (std::vector< MemoryObject* >::iterator 
         it=memoryObjects.begin(), ie=memoryObjects.end(); ie!=it; ++it) {
      delete *it;
    }
  }
};

typedef std::pair<llvm::Instruction*, SymbolicInstance*> SymbolicInstancePair;
typedef std::map<llvm::Instruction*, SymbolicInstance*> SymbolicInstanceMap;

class NuklearManager {
public:
  NuklearManager(Executor &_executor);

  void executeSocketWrite(ExecutionState &state,
                          KInstruction *target,
                          ref<Expr> socket_id,
                          ref<Expr> address,
                          ref<Expr> len);
 
  void executeSocketRead(ExecutionState &state,
                          KInstruction *target,
                          ref<Expr> socket_id,
                          ref<Expr> address,
                          ref<Expr> len);
 
  NuklearSocket* getSocketFromExecutionState(ExecutionState *s, int id);

  void merge();
  void checkpointMerge();

  void addNewSymbolic(const Array* array, const MemoryObject*);
  const Array* getOrCreateArray(ExecutionState &state, std::string &name, 
                                unsigned size);

  SymbolicInstance* getOrCreateSymbolicInstance(ExecutionState &state, 
                                                KInstruction *target,
                                                std::string &name);

  void copySymbolicInstance(ExecutionState &state, 
                                       KInstruction *target,
                                       std::string &name,
                                       MemoryObject *mo,
                                       ObjectState *os);


  void executeAssignSymbolic(ExecutionState &state, 
                             KInstruction *target,
                             std::string &name,
                             ref<Expr> address);

  void printCurrentRoundLog(std::ostream &os);
  unsigned getRoundNumber() { return roundNumber; }
  void printRoundStatistics();
  void printPruneStatistics();
  void updateRoundStatistics();
  void nextRoundStatisticRecord();
  bool checkValidSocketRound(ExecutionState &state);
  void handleXEventsQueued(ExecutionState &state, KInstruction *target);

	std::set<std::string> getCurrentSymbolicNames();

private:
  std::string liveMessageStr(ObjectState *obj, size_t len);
  std::string logMessageStr(unsigned char* bytes, size_t len);
  StatisticRecord *contextStats;
  Executor &executor;
  std::string socketReadStr;
  std::string socketWriteStr;
  std::string socketWriteDropStr;
  unsigned roundNumber;
  unsigned inputLoops;
  std::deque< SymbolicInstanceMap* > symbolics;
  std::deque< StatisticRecord* > roundStats;

};

} // end namespace klee
#endif
/* NUKLEAR KLEE end (ENTIRE FILE) */
