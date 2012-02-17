//===-- ExecutionStateProperty.h --------------------------------*- C++ -*-===//
//
// <insert license>
//
//===----------------------------------------------------------------------===//
//
//===----------------------------------------------------------------------===//
#ifndef CLIVER_EXECUTION_STATE_PROPERTY_H
#define CLIVER_EXECUTION_STATE_PROPERTY_H

#include "cliver/PathManager.h"

#include <llvm/ADT/PriorityQueue.h>

#include <map>
#include <set>
#include <iostream>

#include <boost/ptr_container/ptr_set.hpp>

namespace cliver {
class CVExecutionState;

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
  virtual void print(std::ostream &os) const {};
	virtual int compare(const ExecutionStateProperty &p) const { assert(0); return 0; }
	virtual ExecutionStateProperty* clone() { assert(0); return NULL; }
};

inline std::ostream &operator<<(std::ostream &os, 
		const ExecutionStateProperty &p) {
  p.print(os);
  return os;
}

struct ExecutionStatePropertyFactory {
	static ExecutionStateProperty* create();
};

////////////////////////////////////////////////////////////////////////////////
 
struct CVExecutionStateLT {
	bool operator()(const CVExecutionState* a, const CVExecutionState* b) const;
};

struct ExecutionStatePropertyLT {
	bool operator()(const ExecutionStateProperty* a, 
			const ExecutionStateProperty* b) const;
};

typedef std::set<CVExecutionState*> ExecutionStateSet;

typedef std::map<ExecutionStateProperty*,
								 ExecutionStateSet,
								 ExecutionStatePropertyLT> ExecutionStatePropertyMap;

typedef llvm::PriorityQueue< CVExecutionState*, 
				                     std::vector<CVExecutionState*>,
														 CVExecutionStateLT > ExecutionStatePriorityQueue;

////////////////////////////////////////////////////////////////////////////////

class LogIndexProperty : public ExecutionStateProperty {
 public: 
	LogIndexProperty();
	LogIndexProperty* clone() { return new LogIndexProperty(*this); }
  void print(std::ostream &os) const;
	int compare(const ExecutionStateProperty &b) const;

	// Property values
	int socket_log_index;
};

////////////////////////////////////////////////////////////////////////////////

class PathProperty : public ExecutionStateProperty {
 public: 
	enum PathPropertyPhase {
		PrepareExecute=0, 
		Execute, 
		EndPhase
	};
	PathProperty();
	PathProperty* clone() { return new PathProperty(*this); }
  void print(std::ostream &os) const;
	int compare(const ExecutionStateProperty &b) const;

	// Property values
	int round;
	PathPropertyPhase phase;
	PathRange path_range;
};

////////////////////////////////////////////////////////////////////////////////

class VerifyProperty : public ExecutionStateProperty {
 public: 
	enum VerifyPropertyPhase {
		PrepareExecute=0, 
		Execute, 
		Active,
		Horizon,
		EndPhase
	};
	VerifyProperty();
	VerifyProperty* clone() { return new VerifyProperty(*this); }
  void print(std::ostream &os) const;
	int compare(const ExecutionStateProperty &b) const;

	// Property values
	int round;
	VerifyPropertyPhase phase;
	PathRange path_range;
};

////////////////////////////////////////////////////////////////////////////////

class EditCostProperty : public ExecutionStateProperty {
 public: 
	EditCostProperty();
	EditCostProperty* clone();
  void print(std::ostream &os) const;
	int compare(const ExecutionStateProperty &b) const;

 public: 
	// Property values
	double edit_cost;
};

////////////////////////////////////////////////////////////////////////////////

class EditDistanceProperty : public ExecutionStateProperty {
 public: 
	EditDistanceProperty();
	EditDistanceProperty* clone();
  void print(std::ostream &os) const;
	int compare(const ExecutionStateProperty &b) const;

 public: 
	// Property values
	int edit_distance;
	bool recompute;
};

////////////////////////////////////////////////////////////////////////////////

class NumSymbolicVarsProperty : public ExecutionStateProperty {
 public: 
  NumSymbolicVarsProperty();
  NumSymbolicVarsProperty* clone();
  void print(std::ostream &os) const;
	int compare(const ExecutionStateProperty &b) const;

 public: 
  int num_symbolic_vars; 
};

////////////////////////////////////////////////////////////////////////////////

} // end namespace cliver
#endif // CLIVER_EXECUTION_STATE_PROPERTY_H
