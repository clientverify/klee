/* NUKLEAR KLEE begin (ENTIRE FILE) */
#include "Common.h"
#include "MemoryManager.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/System/Process.h"
#include "llvm/Support/raw_ostream.h"
#include "klee/Internal/System/Time.h"
#include "klee/Internal/Support/Timer.h"
#include "NuklearManager.h"
#include "NuklearHash.h"
#include "CoreStats.h"
#include "Memory.h"

#include "Executor.h"
#include "TimingSolver.h"
#include "../Solver/SolverStats.h"

#include <cassert>
#include <algorithm>
#include <iostream>
#include <iomanip>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>

 
using namespace llvm;
using namespace klee;

namespace {
  cl::opt<bool>
  NoRecvReturnZero("nuklear-no-recv-return-zero");
  cl::opt<bool>
  DebugPrintMessages("nuklear-print-messages");
  cl::opt<bool>
  CapmanMode("nuklear-capman-mode");
  cl::opt<bool>
  XPilotMode("nuklear-xpilot-mode");
  cl::opt<bool>
  XPilotHash("nuklear-xpilot-hash",cl::init(false));
  cl::opt<bool>
  NoSymbolicPrint("nuklear-no-symbolic-print",cl::init(false));
  cl::opt<bool>
  XEventOptimization("xevent-optimization");
  cl::opt<unsigned>
  QUEUE_SIZE("queue-size", cl::init(3));
  cl::opt<unsigned>
  MAX_ROUNDS("max-rounds", cl::init(2000));

  // Debugging level for shrink wrapping.
  enum NuklearDebugLevel {
    None, BasicInfo, Details
  };

  static cl::opt<enum NuklearDebugLevel>
  NuklearDebugging("nuklear-dbg", cl::Hidden,
    cl::desc("Print nuklear debugging information"),
    cl::values(
      clEnumVal(None      , "disable debug output"),
      clEnumVal(BasicInfo,  "print rare/important events"),
      clEnumVal(Details,    "print events and data"),
      clEnumValEnd));

  cl::opt<bool>
  NuklearDebugSocketFailure("nuklear-dbg-socket-failure");

  cl::opt<bool>
  NuklearDebugSocketSuccess("nuklear-dbg-socket-success");


}

static int readRoundNumberFromBuffer(unsigned char* buf) {
    return (int)(((unsigned)buf[0] << 24) 
            | ((unsigned)buf[1] << 16) 
            | ((unsigned)buf[2] << 8) 
            | ((unsigned)buf[3]));
}

NuklearManager::NuklearManager(Executor &_executor) 
 : executor(_executor),
  // Note: strings are swapped (server's point of view)
  socketReadStr("s2c"), socketWriteStr("c2s"), socketWriteDropStr("dc2s"),
  roundNumber(0) { 

  //if (XPilotMode) {
  //  socketReadStr = std::string("s2c"); 
  //  socketWriteStr = std::string("c2s");
  //}
  //if (CapmanMode) {
  //  socketReadStr = std::string("W"); 
  //  socketWriteStr = std::string("R");
  //}

  symbolics.push_back(new SymbolicInstanceMap());

  updateRoundStatistics();
  nextRoundStatisticRecord();
  
  if (NuklearDebugging >= BasicInfo)
    llvm::errs() <<"NUKLEAR NuklearManager constructed.\n";
}

NuklearSocket* NuklearManager::getSocketFromExecutionState(ExecutionState *s, 
                                                           int id) {
  NuklearSocket *nuklear_socket = NULL;
  std::map<int, NuklearSocket>::iterator it = s->nuklearSockets.find(id);

  if (it != s->nuklearSockets.end()) {
    nuklear_socket = &(it->second);
  } else {
    // Lazy Init: First time s->nuklearSockets[id] has been accessed.
    std::map<int, const struct KTest*>::iterator nit = 
      executor.socketKTests.find(id);
    if (nit != executor.socketKTests.end()) {
      const struct KTest *ktest = nit->second;
      s->nuklearSockets[id] = NuklearSocket(ktest);
      nuklear_socket = &(s->nuklearSockets[id]);

    } else {
      std::stringstream ss;
      std::map<int, const struct KTest*>::iterator ti 
        = executor.socketKTests.begin();
      std::map<int, const struct KTest*>::iterator te
        = executor.socketKTests.end();
      ss << "nuklear socket: invalid socket id";
      ss << ", valid ids = { ";
      for (; ti!=te; ++ti) 
        ss << ti->first << " ";
      ss << " }";
      llvm::errs() << "KLEE: ERROR: " << ss.str() << "\n";
      executor.terminateState(*s);
      return NULL;
    }
  }
  return nuklear_socket;
}

std::string NuklearManager::logMessageStr(unsigned char* bytes, size_t len) {
  std::stringstream ss;
  if (NuklearDebugging >= Details) {
    ss << "\nLog [" << len << "] ";
    if (CapmanMode) {
      for (unsigned i=0; i<len; i++) {
        char c = bytes[i];
        ss << c << ":";
      }
    } else {
      for (unsigned i=0; i<len; i++)
        ss << std::dec << (int)bytes[i] << ":";
    }
  }
  return ss.str();
}

std::string NuklearManager::liveMessageStr(ObjectState *obj, size_t len) {
  std::stringstream ss;
  if(NuklearDebugging >= Details) {
    ss << "\nBuf [" << len << "] ";
    if (CapmanMode) {
      for (unsigned i=0; i<len; i++) {
        ref<Expr> e = obj->read8(i);
        if (klee::ConstantExpr *CE = dyn_cast<klee::ConstantExpr>(e)) {
          char c = cast<ConstantExpr>(e)->getZExtValue();
          ss << c << ":";
        } else {
          if (NoSymbolicPrint) 
            ss << "??:";
          else
            ss << e << ":";
        }
      }
    } else {
      for (unsigned i=0; i<len; i++) {
        ref<Expr> e = obj->read8(i);
        if (klee::ConstantExpr *CE = dyn_cast<klee::ConstantExpr>(e)) {
          int c = cast<ConstantExpr>(e)->getZExtValue();
          ss << std::dec << c << ":";
        } else {
          if (NoSymbolicPrint) 
            ss << "??:";
          else
            ss << e << ":";
        }
      }
    }
  }
  return ss.str();
}


/*
 * Socket Read/Write Semantics:
 *
 * Client action for each log type on a Recv in the ith round:
 *    LogRecv_i       return message
 *    LogRecv_i+1     return 0 
 *    LogSend_i       return 0 
 *    LogSend_i+1     return 0 
 *
 * Client action for each log type on a Send in the ith round:
 *    LogRecv_i       terminate
 *    LogRecv_i+1     terminate
 *    LogSend_i       add constraint 
 *    LogSend_i+1     terminate
 */

void NuklearManager::executeSocketWrite(ExecutionState &state,
                                        KInstruction *target,
                                        ref<Expr> socket_id,
                                        ref<Expr> address,
                                        ref<Expr> len) {
 
  int id = cast<ConstantExpr>(socket_id)->getZExtValue();
  int size = cast<ConstantExpr>(len)->getZExtValue();
  int resolve_count = 0;

  Executor::ExactResolutionList rl;
  executor.resolveExact(state, address, rl, "executeSocketWrite");
  
  for (Executor::ExactResolutionList::iterator it = rl.begin(), 
         ie = rl.end(); it != ie; ++it) {
    assert( resolve_count++ == 0 && "Multiple resolutions");

    MemoryObject *mo = (MemoryObject*) it->first.first;
    ObjectState *os = const_cast<ObjectState*>(it->first.second);
    ExecutionState *s = it->second;

    // Fetch socket object
    NuklearSocket *nuklear_socket = getSocketFromExecutionState(s, id);
    assert(NULL != nuklear_socket);

    if (nuklear_socket->index == nuklear_socket->ktest->numObjects) {
      if (NuklearDebugSocketFailure)
        llvm::errs() << "NUKLEAR SEND: FAILURE (End of Log) idx: "
          << nuklear_socket->index << "\n";
      executor.terminateState(*s);
      return;
    }

    KTestObject* obj = &(nuklear_socket->ktest->objects[nuklear_socket->index]);

    unsigned char *logBuf = obj->bytes;
    unsigned logBufSize = obj->numBytes;
    int logRoundNumber = -1;
    std::string name(obj->name);

    /* debug print name */
    //llvm::errs() << "NAME: " << name << " INDEX: " << nuklear_socket->index << "\n";
    /* debug print name */

    if (XPilotMode && logBufSize >= 4) {
      // Length of actual client message
      logBufSize -=4;
      // Extract round id from first 4 bytes
      logRoundNumber = readRoundNumberFromBuffer(logBuf);
      // Advance logBuf pointer, remaining bytes are client's message
      logBuf = logBuf+4;
    }

    std::stringstream ssInfo;
    if (NuklearDebugSocketFailure || NuklearDebugSocketSuccess) {
      ssInfo << " RN: " << roundNumber;
      if (logRoundNumber > 0) ssInfo << ", LogRN: " << logRoundNumber;
      ssInfo << ", idx: " << nuklear_socket->index
        << ", fd: " << socket_id << ", state: " << s->id;
    }

    // rcochran - tmp hack, this shouldn't happen...
    //assert((!XPilotMode || logRoundNumber >= roundNumber) && "logRN < RN");
    if (XPilotMode && logRoundNumber < roundNumber ) {
      if (NuklearDebugSocketFailure) {
        llvm::errs() << "NUKLEAR SEND: FAILURE (logRN < RN) "
           << " " << ssInfo.str() << "\n";
      }
      executor.terminateState(*s);
      return;
    }

    if (name.substr(0, socketWriteStr.size()) != socketWriteStr) {
      if (name.substr(0, socketWriteDropStr.size()) == socketWriteDropStr) {
        // If this packet was dropped in the original trace, we'll have no idea
        // of it's actual contents, we reconstruct the hash from future msgs
  
        ref<Expr> write_condition = ConstantExpr::alloc(1, Expr::Bool);
        bool false_condition = false;

        /* ---------------------------------------------------------------*/
        NuklearHash *nh = new NuklearHash(nuklear_socket);
        if (nh->recover_hash(nuklear_socket->index) == -1) {
					//executor.terminateState(*s);
					//return;
				}
        /* ---------------------------------------------------------------*/

        /* Debug Output --------------------------------------------------*/

        std::stringstream ss_hash;
        if (NuklearDebugSocketFailure || NuklearDebugSocketSuccess) {
          std::stringstream ss_mask;
          int mask_ones_count = 0;
          for (unsigned i=(8*nh->_vals_len) - nh->_dbsize; i<8*(nh->_vals_len); i++) {
            if (nh->_mask_bitarray->get(i)) {
              ss_mask << "1";
              mask_ones_count++;
            } else {
              ss_mask << "0";
            }
          }

          ss_hash << "Recovered " << mask_ones_count 
            << " of " << nh->_hashval_len*8 << " hash bits (" << ss_mask.str() << ")";
        }

        /* ---------------------------------------------------------------*/

        for (unsigned i=0; i<nh->_vals_len; i++) {

          ref<Expr> sym_hash_value
            = AndExpr::create(os->read8(i),
                              ConstantExpr::alloc(nh->_mask[i], Expr::Int8));

          ref<Expr> log_hash_value
            = AndExpr::create(ConstantExpr::alloc(nh->_vals[i], Expr::Int8),
                              ConstantExpr::alloc(nh->_mask[i], Expr::Int8));

          //if (NuklearDebugWriteDetails) {
          //  llvm::errs() << "NUKLEAR HASH: symbolic:        " << sym_hash_value << "\n";
          //  llvm::errs() << "NUKLEAR HASH: log reconstruct: " << log_hash_value << "\n";
          //}

          ref<Expr> condition = EqExpr::create(sym_hash_value, log_hash_value);

          if (ConstantExpr *CE = dyn_cast<ConstantExpr>(condition)) {
            if (CE->isFalse()) {
              false_condition = true;
              break;
            }
          }

          write_condition = AndExpr::create(write_condition, condition);
        }

        /* ---------------------------------------------------------------*/

        if (ConstantExpr *CE = dyn_cast<ConstantExpr>(write_condition)) {
          if (CE->isFalse()) {
            false_condition = true;
          }
        } else {
          bool res;
          executor.solver->mustBeFalse(*s, write_condition, res);
          if (res) 
            false_condition = true;
        }
  
        if (false_condition) {
          if (NuklearDebugSocketFailure) {
						std::stringstream ss;
            ss << "NUKLEAR SEND: FAILURE (Hash Not Satisfiable) "
               << ss_hash.str() << ssInfo.str() 
               << liveMessageStr(os, size) << "\n";

            if (NuklearDebugging >= Details) {
              ss  << "REC: ";
              for (unsigned i=0; i<nh->_vals_len; i++) {
                ss << ConstantExpr::alloc(nh->_vals[i], Expr::Int8) << ":";
              }
              ss<< "\nMSK: ";
              for (unsigned i=0; i<nh->_vals_len; i++) {
                ss << ConstantExpr::alloc(nh->_mask[i], Expr::Int8) << ":";
              }
							llvm::errs() << ss.str() << "\n";
            }
          }
          executor.terminateState(*s);

        } else {
          if (NuklearDebugSocketSuccess) {
						std::stringstream ss;
            ss << "NUKLEAR SEND: SUCCESS (Hash Was Satisfiable) "
              << ss_hash.str() << ssInfo.str() 
              << liveMessageStr(os, size) << "\n";

            if (NuklearDebugging >= Details) {
              ss  << "REC: ";
              for (unsigned i=0; i<nh->_vals_len; i++) {
                ss << ConstantExpr::alloc(nh->_vals[i], Expr::Int8) << ":";
              }
              for (unsigned i=0; i<nh->_vals_len; i++) {
                ss << ConstantExpr::alloc(nh->_mask[i], Expr::Int8) << ":";
              }
              llvm::errs() << ss.str() << "\n";
            }
          }
          nuklear_socket->index++;
          nuklear_socket->bytes = 0;
          executor.addConstraint(*s, write_condition);
          executor.bindLocal(target, *s, ConstantExpr::alloc(size, Expr::Int32));
        }
      }
      // Current object is a not a WRITE object or is from a future round
      else {
        if (NuklearDebugSocketFailure)
          llvm::errs() << "NUKLEAR SEND: FAILURE (Out of Order) "
            << name.substr(0, socketWriteStr.size()) << " != " << socketWriteStr
            << ssInfo.str() << "\n";
        executor.terminateState(*s);
      }
    } else if (XPilotMode && roundNumber != logRoundNumber) {
      // Current object is a not a WRITE object or is from a future round
      
      if (NuklearDebugSocketFailure) 
        llvm::errs() << "NUKLEAR SEND: FAILURE (Early Send) " << ssInfo.str() << "\n";

      executor.terminateState(*s);

    } else if (logBufSize != size) {
      // Socket not writing number of bytes we expect. FIXME? alter semantics
      // to allow size < logBufSize, i.e., multiple calls to write.
      
      if (NuklearDebugSocketFailure) 
        llvm::errs() << "NUKLEAR SEND: FAILURE (Message Size) " << logBufSize
          << "(BUF) != " << size << "(LOG) " << ssInfo.str() 
          << liveMessageStr(os, size) << logMessageStr(logBuf, logBufSize) << "\n";
      executor.terminateState(*s);

    } else {

      ref<Expr> write_condition = ConstantExpr::alloc(1, Expr::Bool);
      bool false_condition = false;

      for (unsigned i=0; i<logBufSize; i++) {
        ref<Expr> condition 
          = EqExpr::create(os->read8(i), 
                           ConstantExpr::alloc(logBuf[i], Expr::Int8));

        if (ConstantExpr *CE = dyn_cast<ConstantExpr>(condition)) {
          if (CE->isFalse()) {
            false_condition = true;
            break;
          }
        }
        write_condition = AndExpr::create(write_condition, condition);
      }

      if (ConstantExpr *CE = dyn_cast<ConstantExpr>(write_condition)) {
        if (CE->isFalse()) {
          false_condition = true;
        }
      } else {
        bool res;
        executor.solver->mustBeFalse(*s, write_condition, res);
        if (res) 
          false_condition = true;
      }
 
      if (false_condition) {
        if (NuklearDebugSocketFailure) 
          llvm::errs() << "NUKLEAR SEND: FAILURE (Not Satisfiable) " << ssInfo.str() 
            << liveMessageStr(os, size) << logMessageStr(logBuf, logBufSize) << "\n";
        executor.terminateState(*s);

      } else {
        if (NuklearDebugSocketSuccess) 
          llvm::errs() << "NUKLEAR SEND: SUCCESS "<< ssInfo.str()
            << liveMessageStr(os, size) << logMessageStr(logBuf, logBufSize) << "\n";
 
        nuklear_socket->index++;
        nuklear_socket->bytes = 0;
        executor.addConstraint(*s, write_condition);
        executor.bindLocal(target, *s, ConstantExpr::alloc(logBufSize, Expr::Int32));
      }
    }
  }

  if (resolve_count == 0) {
    if (NuklearDebugSocketFailure) 
      llvm::errs() << "NUKLEAR SEND: FAILURE (No Resolution) \n";
    executor.terminateState(state);
  }
}

bool NuklearManager::checkValidSocketRound(ExecutionState &state) {
  if (XPilotMode) {
    std::map<int, NuklearSocket>::iterator it = state.nuklearSockets.begin();
    std::map<int, NuklearSocket>::iterator ie = state.nuklearSockets.end();
    for (;it!=ie; ++it) {
      NuklearSocket *ns = &(it->second);
      KTestObject* obj = &(ns->ktest->objects[ns->index]);
      int roundNumberBuf = readRoundNumberFromBuffer(obj->bytes);

      /* Debug Info --------------------------------------------*/
      std::stringstream ss;
      if (NuklearDebugging >= BasicInfo) {
        ss << " name: " << obj->name
          << ", curr_round#: " << roundNumber
          << ", buf_round#: " << roundNumberBuf
          << ", log_index: " << ns->index
          << ", fd: " << it->first << ", state: " << state.id;
          //<< logMessageStr(obj->bytes, obj->numBytes);
      }

      if (roundNumberBuf <= roundNumber) {
        if (NuklearDebugging >= BasicInfo) {
          llvm::errs() << "NUKLEAR RNCHECK: FAIL " << ss.str() << "\n";
        }
        return false;
      } else {
        if (NuklearDebugging >= BasicInfo) {
          llvm::errs() << "NUKLEAR RNCHECK: SUCCESS " << ss.str() << "\n";
        }
      }
    }
  }
  return true;
}

void NuklearManager::handleXEventsQueued(ExecutionState &state,
                                         KInstruction *target) {
  if (XEventOptimization) {
    std::map<int, NuklearSocket>::iterator it = state.nuklearSockets.begin();
    std::map<int, NuklearSocket>::iterator ie = state.nuklearSockets.end();
    bool c2sNext = false;
    for (;it!=ie; ++it) {
      NuklearSocket *ns = &(it->second);
      KTestObject* obj = &(ns->ktest->objects[ns->index]);
      std::string name(obj->name);

      if (name.substr(0, socketWriteDropStr.size()) == socketWriteDropStr) {
        c2sNext = true;
      } else {
        int roundNumberBuf = readRoundNumberFromBuffer(obj->bytes);
        if (roundNumberBuf == roundNumber) {
          if (name.substr(0, socketWriteStr.size()) == socketWriteStr) {
            c2sNext = true;
          }
        }
      }
    }

    if (c2sNext) {
      inputLoops++;
      executor.bindLocal(target, state, ConstantExpr::alloc(QUEUE_SIZE, Expr::Int32));
    } else {
      executor.bindLocal(target, state, ConstantExpr::alloc(0, Expr::Int32));
    }
  } else {
    executor.bindLocal(target, state, ConstantExpr::alloc(QUEUE_SIZE, Expr::Int32));
  }
}


void NuklearManager::printCurrentRoundLog(std::ostream &os) {
  if (XPilotMode) {
    if (NuklearDebugging >= BasicInfo) {
      int log_count = 0;
      std::map<int, const struct KTest*>::iterator it=executor.socketKTests.begin();
      std::map<int, const struct KTest*>::iterator ie=executor.socketKTests.end();
      std::stringstream ss;
      for (;it!=ie; ++it) {
        const KTest* ktest = it->second;
        for (unsigned i=0; i<ktest->numObjects; ++i) {
          KTestObject *obj = &ktest->objects[i];
          unsigned logRoundNumber = readRoundNumberFromBuffer(obj->bytes);
          if (logRoundNumber > roundNumber) {
            break;
          } else if (logRoundNumber == roundNumber) {
            log_count++;
            ss << "idx: " << i << ", type: " << obj->name;
            ss << " [" << obj->numBytes << "] ";
            for (unsigned j=0; j<obj->numBytes; j++)
              ss << std::dec << (int)obj->bytes[j] << ":";
            ss << "\n";
          } else {
            assert(logRoundNumber < roundNumber);
          }
        }
      }
      os << "NUKLEAR Round " << roundNumber << " contains " << log_count <<" log(s).\n";
      os << ss.str();
    }
  }
}

/*
 * Socket Read/Write Semantics:
 *
 * Client action for each log type on a Recv in the ith round:
 *    LogRecv_i       return message
 *    LogRecv_i+1     return 0 
 *    LogSend_i       return 0 
 *    LogSend_i+1     return 0 
*/
void NuklearManager::executeSocketRead(ExecutionState &state,
                                       KInstruction *target,
                                       ref<Expr> socket_id,
                                       ref<Expr> address,
                                       ref<Expr> len) {
 
  int id = cast<ConstantExpr>(socket_id)->getZExtValue();
  int size = cast<ConstantExpr>(len)->getZExtValue();
  int resolve_count = 0;

  Executor::ExactResolutionList rl;
  executor.resolveExact(state, address, rl, "executeSocketWrite");
  
  for (Executor::ExactResolutionList::iterator it = rl.begin(), 
         ie = rl.end(); it != ie; ++it) {
    assert( resolve_count++ == 0 && "Multiple resolutions");

    MemoryObject *mo = (MemoryObject*) it->first.first;
    ObjectState *os = const_cast<ObjectState*>(it->first.second);
    ExecutionState *s = it->second;

    NuklearSocket *nuklear_socket = getSocketFromExecutionState(s, id);

    KTestObject* obj = &(nuklear_socket->ktest->objects[nuklear_socket->index]);

    if (nuklear_socket->index == nuklear_socket->ktest->numObjects) {
      if (NuklearDebugSocketSuccess) 
        llvm::errs() << "NUKLEAR RECV: RETURNING 0 (End of Log) log_index: "
          << nuklear_socket->index << "\n";
      executor.bindLocal(target, *s, ConstantExpr::alloc(0, Expr::Int32));
      return;
    }

    unsigned char *logBuf = obj->bytes;
    unsigned logBufSize = obj->numBytes;
    unsigned logRoundNumber = -1;
    std::string name(obj->name);

    if (XPilotMode && logBufSize >= 4) {
      // Length of actual client message
      logBufSize -=4;
      // Extract round id from first 4 bytes
      logRoundNumber = readRoundNumberFromBuffer(logBuf);
      // Advance logBuf pointer, remaining bytes are client's message
      logBuf = logBuf+4;
    }

    std::stringstream ssInfo;
    if (NuklearDebugSocketSuccess || NuklearDebugSocketFailure) {
      ssInfo << " RN: " << roundNumber;
      if (logRoundNumber > 0) ssInfo << ", LogRN: " << logRoundNumber;
      ssInfo << ", idx: " << nuklear_socket->index
        << ", fd: " << socket_id << ", state: " << s->id;
    }

    assert((!XPilotMode || logRoundNumber >= roundNumber) && "logRN < RN");
    // rcochran - tmp hack, this shouldn't happen...
    //if (XPilotMode && logRoundNumber < roundNumber ) {
    //    llvm::errs() << "NUKLEAR RECV: FAILURE (logRN < RN) "
    //      << roundNumber << " != " 
    //      << logRoundNumber << " " 
    //      << ssInfo.str() << "\n";
    //  executor.terminateState(*s);
    //  return;
    //}

    // Semantics of nuklear socket read: Up to len bytes of obj are copied into
    // the caller's buffer at the given address. If len < logBufSize, the
    // next attempt to read this socket will return up to len remain bytes of
    // obj, and so on, until all logBufSize have been given to caller, at
    // which point the next attempt to read the socket will return 0 and the
    // index variable will be incremented.

    if (name.substr(0, socketReadStr.size()) != socketReadStr) {
      // Current object is a not a READ object.
      if (NuklearDebugSocketSuccess && XPilotMode || 
          NuklearDebugSocketFailure && !XPilotMode) {
        std::string status = XPilotMode ? "RETURNING 0" : "FAILURE";
        llvm::errs() << "NUKLEAR RECV: " << status << " (Out of Order) "
          << name.substr(0, socketReadStr.size())
          << " != " << socketReadStr << ssInfo.str() 
          << logMessageStr(logBuf, logBufSize) << "\n";
      }
      if (XPilotMode)
        executor.bindLocal(target, *s, ConstantExpr::alloc(0, Expr::Int32));
      else 
        executor.terminateState(*s);

    } else if (XPilotMode && roundNumber != logRoundNumber ) {
      // Early Read, return 0
      if (NuklearDebugSocketSuccess) 
        llvm::errs() << "NUKLEAR RECV: RETURNING 0 (Early Read) RN != LogRN "
          << ssInfo.str() << "\n";
      executor.bindLocal(target, *s, ConstantExpr::alloc(0, Expr::Int32));
 
    } else if (logBufSize == nuklear_socket->bytes) {
      // We have copied all of the obj in the previous 1-N calls, we now 
      // return 0 (nothing left to read on socket) and advance the 
      // index index.
      executor.bindLocal(target, *s, ConstantExpr::alloc(0, Expr::Int32));
      nuklear_socket->index++;
      nuklear_socket->bytes = 0;
    } else {

      unsigned copy_len = logBufSize - nuklear_socket->bytes;

      if (copy_len > size) copy_len = size;

      for (unsigned i=0; i<copy_len; i++)
        os->write8(i, logBuf[i + nuklear_socket->bytes]);

      nuklear_socket->bytes += copy_len;

      if (NuklearDebugSocketSuccess) 
        llvm::errs() << "NUKLEAR RECV: SUCCESS " << ssInfo.str() 
          << logMessageStr(logBuf, logBufSize) << "\n";

      // If we want to model for uses of recv that make repeated calls until
      // there is nothing left to read on the resource.
      if (NoRecvReturnZero && logBufSize == nuklear_socket->bytes) {
        nuklear_socket->index++;
        nuklear_socket->bytes = 0;
      } else if (!NoRecvReturnZero && nuklear_socket->bytes == obj->numBytes) {
        nuklear_socket->index++;
        nuklear_socket->bytes = 0;
      }
      executor.bindLocal(target, *s, ConstantExpr::alloc(copy_len, Expr::Int32));
    }
  }

  // This should never happen?
  if (resolve_count == 0) {
    if (NuklearDebugSocketFailure) 
      llvm::errs() << "NUKLEAR RECV: FAILURE (No Resolution) \n";
    executor.terminateState(state);
  }
}

void NuklearManager::printRoundStatistics() {
  StatisticRecord *sr = roundStats.back();
  llvm::errs() << "STATS " << roundNumber 
    << " " << sr->getValue(stats::nuklearRoundStates)
    << " " << sr->getValue(stats::nuklearStateErases)
    << " " << sr->getValue(stats::nuklearPrunes)
    << " " << sr->getValue(stats::nuklearRoundTime) / 1000000.
    << " " << sr->getValue(stats::nuklearRoundTimeReal) / 1000000.
    << " " << sr->getValue(stats::nuklearPruneTime) / 1000000.
    << " " << sr->getValue(stats::nuklearMergeTime) / 1000000.
    << " " << sr->getValue(stats::nuklearMerges)
    << " " << sr->getValue(stats::nuklearPreMergeStates)
    << " " << inputLoops
    << " " << sys::Process::GetTotalMemoryUsage()
    << "\n";
}

void NuklearManager::printPruneStatistics() {
  StatisticRecord *sr = roundStats.back();
  llvm::errs() << "\tPruning Stats: " << roundNumber 
    << " " << sr->getValue(stats::nuklearPruneTime) / 1000000.
    << " " << sr->getValue(stats::nuklearPruneTime_stack) / 1000000.
    << " " << sr->getValue(stats::nuklearPruneTime_addressSpace) / 1000000.
    << " " << sr->getValue(stats::nuklearPruneTime_symprune) / 1000000.
    << " " << sr->getValue(stats::nuklearPruneTime_constraintprune) / 1000000.
    << "\n";
}

void NuklearManager::updateRoundStatistics() {

  static sys::TimeValue lastNowTime(0,0),lastUserTime(0,0);

  if (lastUserTime.seconds()==0 && lastUserTime.nanoseconds()==0) {
    sys::TimeValue sys(0,0);
    sys::Process::GetTimeUsage(lastNowTime,lastUserTime,sys);
  } else {
    sys::TimeValue now(0,0),user(0,0),sys(0,0);
    sys::Process::GetTimeUsage(now,user,sys);
    sys::TimeValue delta = user - lastUserTime;
    sys::TimeValue deltaNow = now - lastNowTime;
    stats::nuklearRoundTime += delta.usec();
    stats::nuklearRoundTimeReal += deltaNow.usec();
    lastUserTime = user;
    lastNowTime = now;
  }

  stats::nuklearRoundStates += executor.states.size();
}

void NuklearManager::nextRoundStatisticRecord() {
  roundStats.push_back(new StatisticRecord());
  theStatisticManager->setNuklearContext(roundStats.back());
}

void NuklearManager::checkpointMerge() {

  //roundNumber++;
  //symbolics.push_back(new SymbolicInstanceMap());

  //updateRoundStatistics();
  //printRoundStatistics();
  //nextRoundStatisticRecord();

  // Rebuild solvers each round to keep caches fresh.
  //delete executor.solver;
  //STPSolver *stpSolver = new STPSolver(false);
  //Solver *solver = createCexCachingSolver(solver);
  //solver = createCachingSolver(solver);
  //solver = createIndependentSolver(solver);
  //executor.solver = new TimingSolver(solver, stpSolver);


  /*
  // XXX(rcochran) failed attempt at freeing some memory...
  if (roundNumber > 5) {
    for (SymbolicInstanceMap::iterator it=symbolics[roundNumber-4]->begin(),
         ie=symbolics[roundNumber-4]->end();
         ie!=it; ++it) {
      delete it->second;
    }
    //delete symbolics.front();
    //symbolics.pop_front();
  }
  */
}


void NuklearManager::merge() {

  roundNumber++;
  symbolics.push_back(new SymbolicInstanceMap());

  updateRoundStatistics();
  printRoundStatistics();
  nextRoundStatisticRecord();
  if (executor.states.size() == 0) {
    llvm::errs() << "No remaining states. Exiting.\n";
    exit(1);
  }
  std::stringstream ss;
  printCurrentRoundLog(ss);
  llvm::errs() << ss.str();

	executor.memory->cleanup();

  // Rebuild solvers each round to keep caches fresh.
  delete executor.solver;
  STPSolver *stpSolver = new STPSolver(false);
  Solver *solver = createCexCachingSolver(stpSolver);
  solver = createCachingSolver(solver);
  solver = createIndependentSolver(solver);
  executor.solver = new TimingSolver(solver, stpSolver);

  inputLoops = 0;

  if (MAX_ROUNDS != 0 && roundNumber > MAX_ROUNDS) {
    exit(0);
  }
}

std::set<std::string> NuklearManager::getCurrentSymbolicNames() {
  SymbolicInstance* si = NULL;
	std::set<std::string> current_symbolic_names;
  unsigned roundIndex = roundNumber;
	SymbolicInstanceMap::iterator it = symbolics[roundIndex]->begin();
	SymbolicInstanceMap::iterator ie = symbolics[roundIndex]->end();
	for (; it != ie; ++it) {
		std::vector<std::string>::iterator n_it = it->second->names.begin();
		std::vector<std::string>::iterator n_ie = it->second->names.end(); 
		for (; n_it!=n_ie; ++n_it) {
			current_symbolic_names.insert(*n_it);
		}
	}
	return current_symbolic_names;
}

SymbolicInstance* 
NuklearManager::getOrCreateSymbolicInstance(ExecutionState &state, 
                                            KInstruction *target,
                                            std::string &name) {
  // Search the SymbolicInstanceMap of the current round for an SymbolicInstance
  // keyed on the target instruction. It will be found if we've reached this
  // instruction before.
  SymbolicInstance* si = NULL;
  unsigned roundIndex = roundNumber;
  {
    SymbolicInstanceMap::iterator it = symbolics[roundIndex]->find(target->inst);
    if (it != symbolics[roundIndex]->end()) {
      // Already initialized this round. (we've been here before)
      si = it->second;
    } else {
      // New SymbolicInstance for this round.
      si = new SymbolicInstance(name, roundNumber);
      symbolics[roundIndex]->insert(SymbolicInstancePair(target->inst, si));
    }
  }

  assert(si != NULL && "SymbolicInstance is NULL.");

  // Determine how many times 'state' has reached 'target' in the current round.
  {
    std::map< unsigned, unsigned>::iterator it = si->users.find(state.id);
    if (it == si->users.end()) {
      // First usage of this symbolic instance for this path. Set to zero.
      si->users[state.id] = 0;
    } else {
      si->users[state.id] = si->users[state.id] + 1;
    }
  }

  if (NuklearDebugging >= Details)
    llvm::errs() << "NUKLEAR SYMBOLIC INSTANCE si->users[" << state.id << "] = " << si->users[state.id] 
      << " si->arrays.size() = " << si->arrays.size() << "\n";

  assert(si->baseName == name && "SymbolicInstance has wrong name");

  return si;
}

void NuklearManager::copySymbolicInstance(ExecutionState &state, 
                                          KInstruction *target,
                                          std::string &name,
                                          MemoryObject *mo,
                                          ObjectState *os) {

  // Get or create the SymbolicInstance
  SymbolicInstance* si = getOrCreateSymbolicInstance(state, target, name);

  ObjectState *sym_os = NULL;
  MemoryObject *sym_mo = NULL;
  const Array *sym_array = NULL;
  ref<Expr> sym_expr;
  std::string sym_name;

  if (si->arrays.size() == 0 || si->users[state.id] > si->arrays.size() - 1) {
    // We are the first state to reach this instruction for the ith time, where
    // i = si->users[state.id], so we need to build a new symbolic object.

    // Name Format: [name]_nuklear_[round-number]_[instance-number]
    std::stringstream ss;
    ss << name << "_nuklear_" << roundNumber << "_" << si->users[state.id];

    // Create and allocate a new symbolic memory object.
    sym_array = new Array(ss.str(), mo->size);
    sym_mo    = executor.memory->allocate(mo->size, false, false, 
                                          state.prevPC->inst);
    sym_os    = new ObjectState(sym_mo, sym_array);
    sym_expr  = sym_os->read(0, sym_mo->size*8);
    sym_name  = ss.str();

    // Set the name.
    sym_mo->setName(sym_name);
    
    // Add the new memory object to the vectors in the SymbolicInstance.
    si->arrays.push_back(sym_array);
    si->names.push_back(sym_name);
    si->exprs.push_back(sym_expr);
    si->memoryObjects.push_back(sym_mo);
    //si->objectStates.push_back(sym_os);

    if (NuklearDebugging >= Details)
      llvm::errs() << "NUKLEAR CREATED SYMBOLIC: " << sym_name << "\n";

    delete sym_os;
  } else {
    // Another state has previously reached 'target' for ith time so we use the
    // symbolic object that they created to ensure consistency accross states.

    sym_array = si->arrays[si->users[state.id]];
    sym_name  = si->names[si->users[state.id]];
    sym_expr  = si->exprs[si->users[state.id]];
    sym_mo    = si->memoryObjects[si->users[state.id]];
    //sym_os    = si->objectStates[si->users[state.id]];

    if (NuklearDebugging >= Details)
      llvm::errs() << "NUKLEAR RETRIEVED SYMBOLIC: " << sym_name << "\n";
  }

  // Bind the symbolic memory object to the address space.
  //state.addressSpace.bindObject(sym_mo, sym_os);

  // Add the symbolic memory object to state.symbolics.
  state.addSymbolic(sym_mo, sym_array);

  // Copy the symbolic memory object into the ObjectState for this path.
  os->write(0, sym_expr);
  mo->setName(sym_name);


}

void NuklearManager::executeAssignSymbolic(ExecutionState &state, 
                                           KInstruction *target,
                                           std::string &name,
                                           ref<Expr> address) {
  Executor::ExactResolutionList rl;
  executor.resolveExact(state, address, rl, "executeAssignSymbolic");
  
  for (Executor::ExactResolutionList::iterator it = rl.begin(), 
         ie = rl.end(); it != ie; ++it) {
   
    bool isLocal;
    MemoryObject *mo = (MemoryObject*) it->first.first;
    ObjectState *os = const_cast<ObjectState*>(it->first.second);
    ExecutionState *s = it->second;
 
    //if (DebugNuklearManager) {
    //  std::string alloc_info;
    //  mo->getAllocInfo(alloc_info);
    //  llvm::errs() << "NuklearManger::executeAssignSymbolic: " 
    //    << alloc_info << "\n";
    //}

    copySymbolicInstance(*s, target, name, mo, os);
  }
}

/* NUKLEAR KLEE end (ENTIRE FILE) */
