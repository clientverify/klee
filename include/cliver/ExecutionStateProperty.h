//===-- ExecutionStateProperty.h --------------------------------*- C++ -*-===//
//
// <insert license>
//
//===----------------------------------------------------------------------===//
//
//===----------------------------------------------------------------------===//
#ifndef CLIVER_EXECUTION_STATE_PROPERTY_H
#define CLIVER_EXECUTION_STATE_PROPERTY_H

#include "cliver/CVExecutionState.h"

#include <llvm/ADT/PriorityQueue.h>

#include <boost/ptr_container/ptr_set.hpp>

#include <map>
#include <set>
#include <iostream>

namespace cliver {

////////////////////////////////////////////////////////////////////////////////

/// Classes that inherit from ExecutionStateProperty act as external "decorators"
/// for CVExecutionStates to describe a certain set of differentiating properties.
/// Each CVExecutionState has an internal ExecutionStateProperty, but a single
/// instance of this class can be used to represent a class of states when combined
/// with stl collections. This is how ExecutionStateProperty is used in the 
/// CVSearcher classes. Instances of ExecutionStateProperty are used to form
/// an ordering of CVExecutionStates, for example, LogIndexProperty uses the 
/// log_index of the NetworkManager to form a class of CVExecutionStates.
/// ExecutionStateProperty should be used as if it was pure virtual, i.e., not
/// used directly. The member variables of these classes are public for now so that 
/// a CVExecutionState is not needed to create an instance. Inherited classes
/// should preserve this functionality through public member variables or functions
/// to create instances without a given CVExecutionState.
///
class ExecutionStateProperty {
 public:
  ExecutionStateProperty();
  virtual void print(std::ostream &os) const;
	virtual int compare(const ExecutionStateProperty &p) const;
	virtual ExecutionStateProperty* clone();
  virtual void reset();

  int round; // current socket id
  int client_round; // client specific round number
  int edit_distance; // edit distance 
  int symbolic_vars; // number of symbolic variables created for this round
	bool recompute;
  bool is_recv_processing;
};

inline std::ostream &operator<<(std::ostream &os, 
		const ExecutionStateProperty &p) {
  p.print(os);
  return os;
}

class EditDistanceExecutionStateProperty : public ExecutionStateProperty {
 public:
  EditDistanceExecutionStateProperty();
	virtual int compare(const ExecutionStateProperty &p) const;
};

inline std::ostream &operator<<(std::ostream &os, 
		const EditDistanceExecutionStateProperty &p) {
  p.print(os);
  return os;
}

struct ExecutionStatePropertyFactory {
	static ExecutionStateProperty* create();
};

////////////////////////////////////////////////////////////////////////////////

struct ExecutionStatePropertyLT {
	bool operator()(const ExecutionStateProperty* a, 
			const ExecutionStateProperty* b) const;
};

typedef std::map<ExecutionStateProperty*,
								 ExecutionStateSet,
								 ExecutionStatePropertyLT> ExecutionStatePropertyMap;

typedef llvm::PriorityQueue< CVExecutionState*, 
				                     std::vector<CVExecutionState*>,
														 CVExecutionStateLT > ExecutionStatePriorityQueue;

typedef llvm::PriorityQueue< ExecutionStateProperty*, 
				                     std::vector<ExecutionStateProperty*>,
														 ExecutionStatePropertyLT > StatePropertyPriorityQueue;

////////////////////////////////////////////////////////////////////////////////

} // end namespace cliver
#endif // CLIVER_EXECUTION_STATE_PROPERTY_H
