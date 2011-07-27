//===-- NetworkManager.cpp --------------------------------------*- C++ -*-===//
//
// <insert license>
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//

#include "NetworkManager.h"
#include "llvm/Support/CommandLine.h"
#include "../Core/Memory.h"

namespace {
enum NetworkModel {
  DefaultNetworkModel
};

llvm::cl::opt<NetworkModel>
cl_network_model("network-model", 
  llvm::cl::desc("Choose the network model."),
  llvm::cl::values(
    clEnumValN(DefaultNetworkModel, "default", 
      "Default network model"),
  clEnumValEnd),
  llvm::cl::init(DefaultNetworkModel));
}

namespace cliver {

NetworkManager::NetworkManager(CVExecutionState* state) 
	: state_(state) {}

NetworkManager* NetworkManager::clone(CVExecutionState *state) {
	NetworkManager* nwm = new NetworkManager(*this);
	nwm->state_ = state;
	return nwm;
}

NetworkManager* NetworkManagerFactory::create(CVExecutionState* state) {
  switch (cl_network_model) {
	case DefaultNetworkModel: 
    break;
  }
  return new NetworkManager(state);
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

//void NuklearManager::executeSocketWrite(ExecutionState &state,
//                                        KInstruction *target,
//                                        ref<Expr> socket_id,
//                                        ref<Expr> address,
//                                        ref<Expr> len) {
// 
//  int id = cast<ConstantExpr>(socket_id)->getZExtValue();
//  int size = cast<ConstantExpr>(len)->getZExtValue();
//  int resolve_count = 0;
//
//  Executor::ExactResolutionList rl;
//  executor.resolveExact(state, address, rl, "executeSocketWrite");
//  
//  for (Executor::ExactResolutionList::iterator it = rl.begin(), 
//         ie = rl.end(); it != ie; ++it) {
//    assert( resolve_count++ == 0 && "Multiple resolutions");
//
//    MemoryObject *mo = (MemoryObject*) it->first.first;
//    ObjectState *os = const_cast<ObjectState*>(it->first.second);
//    ExecutionState *s = it->second;
//
//    // Fetch socket object
//    NuklearSocket *nuklear_socket = getSocketFromExecutionState(s, id);
//    assert(NULL != nuklear_socket);
//
//    if (nuklear_socket->index == nuklear_socket->ktest->numObjects) {
//      if (NuklearDebugSocketFailure)
//        llvm::errs() << "NUKLEAR SEND: FAILURE (End of Log) idx: "
//          << nuklear_socket->index << "\n";
//      executor.terminateState(*s);
//      return;
//    }
//
//    KTestObject* obj = &(nuklear_socket->ktest->objects[nuklear_socket->index]);
//
//    unsigned char *logBuf = obj->bytes;
//    unsigned logBufSize = obj->numBytes;
//    int logRoundNumber = -1;
//    std::string name(obj->name);
//
//    /* debug print name */
//    //llvm::errs() << "NAME: " << name << " INDEX: " << nuklear_socket->index << "\n";
//    /* debug print name */
//
//    if (XPilotMode && logBufSize >= 4) {
//      // Length of actual client message
//      logBufSize -=4;
//      // Extract round id from first 4 bytes
//      logRoundNumber = readRoundNumberFromBuffer(logBuf);
//      // Advance logBuf pointer, remaining bytes are client's message
//      logBuf = logBuf+4;
//    }
//
//    std::stringstream ssInfo;
//    if (NuklearDebugSocketFailure || NuklearDebugSocketSuccess) {
//      ssInfo << " RN: " << roundNumber;
//      if (logRoundNumber > 0) ssInfo << ", LogRN: " << logRoundNumber;
//      ssInfo << ", idx: " << nuklear_socket->index
//        << ", fd: " << socket_id << ", state: " << s->id;
//    }
//
//    // rcochran - tmp hack, this shouldn't happen...
//    //assert((!XPilotMode || logRoundNumber >= roundNumber) && "logRN < RN");
//    if (XPilotMode && logRoundNumber < roundNumber ) {
//      if (NuklearDebugSocketFailure) {
//        llvm::errs() << "NUKLEAR SEND: FAILURE (logRN < RN) "
//           << " " << ssInfo.str() << "\n";
//      }
//      executor.terminateState(*s);
//      return;
//    }
//
//    if (name.substr(0, socketWriteStr.size()) != socketWriteStr) {
//      if (name.substr(0, socketWriteDropStr.size()) == socketWriteDropStr) {
//        // If this packet was dropped in the original trace, we'll have no idea
//        // of it's actual contents, we reconstruct the hash from future msgs
//  
//        ref<Expr> write_condition = ConstantExpr::alloc(1, Expr::Bool);
//        bool false_condition = false;
//
//        /* ---------------------------------------------------------------*/
//        NuklearHash *nh = new NuklearHash(nuklear_socket);
//        if (nh->recover_hash(nuklear_socket->index) == -1) {
//					//executor.terminateState(*s);
//					//return;
//				}
//        /* ---------------------------------------------------------------*/
//
//        /* Debug Output --------------------------------------------------*/
//
//        std::stringstream ss_hash;
//        if (NuklearDebugSocketFailure || NuklearDebugSocketSuccess) {
//          std::stringstream ss_mask;
//          int mask_ones_count = 0;
//          for (unsigned i=(8*nh->_vals_len) - nh->_dbsize; i<8*(nh->_vals_len); i++) {
//            if (nh->_mask_bitarray->get(i)) {
//              ss_mask << "1";
//              mask_ones_count++;
//            } else {
//              ss_mask << "0";
//            }
//          }
//
//          ss_hash << "Recovered " << mask_ones_count 
//            << " of " << nh->_hashval_len*8 << " hash bits (" << ss_mask.str() << ")";
//        }
//
//        /* ---------------------------------------------------------------*/
//
//        for (unsigned i=0; i<nh->_vals_len; i++) {
//
//          ref<Expr> sym_hash_value
//            = AndExpr::create(os->read8(i),
//                              ConstantExpr::alloc(nh->_mask[i], Expr::Int8));
//
//          ref<Expr> log_hash_value
//            = AndExpr::create(ConstantExpr::alloc(nh->_vals[i], Expr::Int8),
//                              ConstantExpr::alloc(nh->_mask[i], Expr::Int8));
//
//          //if (NuklearDebugWriteDetails) {
//          //  llvm::errs() << "NUKLEAR HASH: symbolic:        " << sym_hash_value << "\n";
//          //  llvm::errs() << "NUKLEAR HASH: log reconstruct: " << log_hash_value << "\n";
//          //}
//
//          ref<Expr> condition = EqExpr::create(sym_hash_value, log_hash_value);
//
//          if (ConstantExpr *CE = dyn_cast<ConstantExpr>(condition)) {
//            if (CE->isFalse()) {
//              false_condition = true;
//              break;
//            }
//          }
//
//          write_condition = AndExpr::create(write_condition, condition);
//        }
//
//        /* ---------------------------------------------------------------*/
//
//        if (ConstantExpr *CE = dyn_cast<ConstantExpr>(write_condition)) {
//          if (CE->isFalse()) {
//            false_condition = true;
//          }
//        } else {
//          bool res;
//          executor.solver->mustBeFalse(*s, write_condition, res);
//          if (res) 
//            false_condition = true;
//        }
//  
//        if (false_condition) {
//          if (NuklearDebugSocketFailure) {
//						std::stringstream ss;
//            ss << "NUKLEAR SEND: FAILURE (Hash Not Satisfiable) "
//               << ss_hash.str() << ssInfo.str() 
//               << liveMessageStr(os, size) << "\n";
//
//            if (NuklearDebugging >= Details) {
//              ss  << "REC: ";
//              for (unsigned i=0; i<nh->_vals_len; i++) {
//                ss << ConstantExpr::alloc(nh->_vals[i], Expr::Int8) << ":";
//              }
//              ss<< "\nMSK: ";
//              for (unsigned i=0; i<nh->_vals_len; i++) {
//                ss << ConstantExpr::alloc(nh->_mask[i], Expr::Int8) << ":";
//              }
//							llvm::errs() << ss.str() << "\n";
//            }
//          }
//          executor.terminateState(*s);
//
//        } else {
//          if (NuklearDebugSocketSuccess) {
//						std::stringstream ss;
//            ss << "NUKLEAR SEND: SUCCESS (Hash Was Satisfiable) "
//              << ss_hash.str() << ssInfo.str() 
//              << liveMessageStr(os, size) << "\n";
//
//            if (NuklearDebugging >= Details) {
//              ss  << "REC: ";
//              for (unsigned i=0; i<nh->_vals_len; i++) {
//                ss << ConstantExpr::alloc(nh->_vals[i], Expr::Int8) << ":";
//              }
//              for (unsigned i=0; i<nh->_vals_len; i++) {
//                ss << ConstantExpr::alloc(nh->_mask[i], Expr::Int8) << ":";
//              }
//              llvm::errs() << ss.str() << "\n";
//            }
//          }
//          nuklear_socket->index++;
//          nuklear_socket->bytes = 0;
//          executor.addConstraint(*s, write_condition);
//          executor.bindLocal(target, *s, ConstantExpr::alloc(size, Expr::Int32));
//        }
//      }
//      // Current object is a not a WRITE object or is from a future round
//      else {
//        if (NuklearDebugSocketFailure)
//          llvm::errs() << "NUKLEAR SEND: FAILURE (Out of Order) "
//            << name.substr(0, socketWriteStr.size()) << " != " << socketWriteStr
//            << ssInfo.str() << "\n";
//        executor.terminateState(*s);
//      }
//    } else if (XPilotMode && roundNumber != logRoundNumber) {
//      // Current object is a not a WRITE object or is from a future round
//      
//      if (NuklearDebugSocketFailure) 
//        llvm::errs() << "NUKLEAR SEND: FAILURE (Early Send) " << ssInfo.str() << "\n";
//
//      executor.terminateState(*s);
//
//    } else if (logBufSize != size) {
//      // Socket not writing number of bytes we expect. FIXME? alter semantics
//      // to allow size < logBufSize, i.e., multiple calls to write.
//      
//      if (NuklearDebugSocketFailure) 
//        llvm::errs() << "NUKLEAR SEND: FAILURE (Message Size) " << logBufSize
//          << "(BUF) != " << size << "(LOG) " << ssInfo.str() 
//          << liveMessageStr(os, size) << logMessageStr(logBuf, logBufSize) << "\n";
//      executor.terminateState(*s);
//
//    } else {
//
//      ref<Expr> write_condition = ConstantExpr::alloc(1, Expr::Bool);
//      bool false_condition = false;
//
//      for (unsigned i=0; i<logBufSize; i++) {
//        ref<Expr> condition 
//          = EqExpr::create(os->read8(i), 
//                           ConstantExpr::alloc(logBuf[i], Expr::Int8));
//
//        if (ConstantExpr *CE = dyn_cast<ConstantExpr>(condition)) {
//          if (CE->isFalse()) {
//            false_condition = true;
//            break;
//          }
//        }
//        write_condition = AndExpr::create(write_condition, condition);
//      }
//
//      if (ConstantExpr *CE = dyn_cast<ConstantExpr>(write_condition)) {
//        if (CE->isFalse()) {
//          false_condition = true;
//        }
//      } else {
//        bool res;
//        executor.solver->mustBeFalse(*s, write_condition, res);
//        if (res) 
//          false_condition = true;
//      }
// 
//      if (false_condition) {
//        if (NuklearDebugSocketFailure) 
//          llvm::errs() << "NUKLEAR SEND: FAILURE (Not Satisfiable) " << ssInfo.str() 
//            << liveMessageStr(os, size) << logMessageStr(logBuf, logBufSize) << "\n";
//        executor.terminateState(*s);
//
//      } else {
//        if (NuklearDebugSocketSuccess) 
//          llvm::errs() << "NUKLEAR SEND: SUCCESS "<< ssInfo.str()
//            << liveMessageStr(os, size) << logMessageStr(logBuf, logBufSize) << "\n";
// 
//        nuklear_socket->index++;
//        nuklear_socket->bytes = 0;
//        executor.addConstraint(*s, write_condition);
//        executor.bindLocal(target, *s, ConstantExpr::alloc(logBufSize, Expr::Int32));
//      }
//    }
//  }
//
//  if (resolve_count == 0) {
//    if (NuklearDebugSocketFailure) 
//      llvm::errs() << "NUKLEAR SEND: FAILURE (No Resolution) \n";
//    executor.terminateState(state);
//  }
//}

void ExternalHandler_socket_read_event(
		klee::Executor* executor,
		klee::ExecutionState *state, 
		klee::KInstruction *target, 
    std::vector<klee::ref<klee::Expr> > &arguments) {

	assert(arguments.size() == 3);

	// Convert arguments from klee::Expr to concrete values
  int id = cast<klee::ConstantExpr>(arguments[0])->getZExtValue();
	klee::ref<klee::Expr> address = arguments[1];
  int len = cast<klee::ConstantExpr>(arguments[2])->getZExtValue();

	// Cast the executor and execution state
	CVExecutor *cv_executor = static_cast<CVExecutor*>(executor);
	CVExecutionState *cv_state = static_cast<CVExecutionState*>(state);

  NetworkManager* network_manager = cv_state->network_manager();

	assert(network_manager->state() == cv_state);

	klee::ObjectPair result;
  cv_executor->resolve_one(state, address, result);

	klee::ObjectState *object = const_cast<klee::ObjectState*>(result.second);

	network_manager->execute_read(cv_executor, target, object, id, len);
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
 * Although the log buffer is technically a stream of bytes, it is broken up into
 * discrete chunks to simulate the passage of time between messages. If the provided
 * receive buffer is not large enough hold the current LogBuffer. We copy as much
 * as possible each time the socket is read until all has been copied. When all bytes
 * have been copied we return 0 and advance the LogBuffer index.
 *
 * Semantics of nuklear socket read: Up to len bytes of obj are copied into
 * the caller's buffer at the given address. If len < logBufSize, the
 * next attempt to read this socket will return up to len remain bytes of
 * obj, and so on, until all logBufSize have been given to caller, at
 * which point the next attempt to read the socket will return 0 and the
 * index variable will be incremented.
 *
*/

void NetworkManager::execute_read(CVExecutor* executor,
		klee::KInstruction *target, klee::ObjectState* object, int id, int len) {

	Socket &socket = sockets_[id];

	if (socket.round() < round()) {
		executor->terminate_state(state_);
		return;
	}

	if (socket.type() != SocketEvent::RECV) {
		executor->bind_local(target, state_, 0);
		return;
	}

	if (socket.round() != round()) {
		executor->bind_local(target, state_, 0);
		return;
	}

	if (socket.state() == Socket::FINISHED) {
		socket.advance();
		executor->bind_local(target, state_, 0);
		return;

	} else {
		unsigned bytes_written = 0;
		while (socket.has_data() && bytes_written < len) {
			object->write8(bytes_written++, socket.next_byte());
		}
		if (!socket.has_data()) {
			socket.set_state(Socket::FINISHED);
		}
		executor->bind_local(target, state_, bytes_written);
		return;
	}
}

//void NetworkManager::execute_read(CVExecutor* executor,
//		klee::KInstruction *target, const klee::ObjectState* object, int id, int len) {
//
//	Socket &socket = sockets_[id];
//
//	// Xpilot
//	if (socket.round() < round()) {
//		executor->terminate_state(state_);
//		return;
//	}
//
//	if (socket.type() != SocketEvent::RECV) {
//		// Xpilot 
//		executor->bind_local(target, state_, 0);
//		// Other
//		//executor->terminate_state(state);
//		return;
//	}
//
//	// Xpilot
//	if (socket.round() != round()) {
//		executor->bind_local(target, state_, 0);
//		return;
//	}
//
//	if (socket.state() == Socket::FINISHED) {
//		socket.advance();
//		executor->bind_local(target, state_, 0);
//		return;
//
//	} else {
//		unsigned bytes_written = 0;
//		while (socket.has_data() && bytes_written < len) {
//			const_cast<klee::ObjectState*>(object)->write8(bytes_written++, socket.next_byte());
//		}
//		if (!socket.has_data()) {
//			socket.set_state(Socket::FINISHED);
//		}
//		executor->bind_local(target, state_, bytes_written);
//		return;
//	}
//}



} // end namespace cliver
