//===-- PathManager.h -------------------------------------------*- C++ -*-===//
//
// <insert license>
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//
#ifndef CLIVER_PATH_MANAGER_H
#define CLIVER_PATH_MANAGER_H

#include "ClientVerifier.h"
#include "Path.h"
#include "Socket.h"
#include "klee/Solver.h"

#include <list>
#include <set>
#include <fstream>
#include <iostream>

#include <boost/serialization/access.hpp>
#include <boost/serialization/vector.hpp>

namespace llvm {
 class Instruction;
}

namespace klee {
 class KInstruction;
}

namespace cliver {

////////////////////////////////////////////////////////////////////////////////

/// PathManager is a wrapper around a single Path that allows for additional
/// information to be associated with a Path. PathManager interface is used by
/// CVExecutor to query whether a particular branch is feasible and to indicate
/// when a particular branch is followed. Every CVExecutionState holds a single
/// PathManager which is cloned when a CVExecutionState is cloned.

class PathManager {
 public:
	PathManager();
	virtual ~PathManager();
	virtual PathManager* clone();
	virtual bool merge(const PathManager &pm);
	virtual bool less(const PathManager &b) const;
	virtual bool try_branch(bool direction, klee::Solver::Validity validity, 
			klee::KInstruction* inst, CVExecutionState *state);
	virtual void branch(bool direction, klee::Solver::Validity validity, 
			klee::KInstruction* inst, CVExecutionState *state);
	virtual void print(std::ostream &os) const;

	void set_range(const PathRange& range);

	unsigned length() { return path_->length(); }
	PathRange& range() { return range_; }
	const Path* path() { return path_; }

 protected:
	explicit PathManager(Path *path, PathRange &range); // only used by clone
	explicit PathManager(const PathManager &pm); // not implemented
	Path* path_;
	PathRange range_;
};

inline std::ostream &operator<<(std::ostream &os, 
		const PathManager &p) {
  p.print(os);
  return os;
}

////////////////////////////////////////////////////////////////////////////////

/// TrainingPathManager is a PathManager that is serializable to file and 
/// maintains a list of messages that are assocated with that Path, i.e., there
/// given some initial state following this Path can lead to a valid event
/// handling of the given message 
class TrainingPathManager : public PathManager {
 public:
	TrainingPathManager();
	TrainingPathManager(Path *path, PathRange &range); // make private?
	virtual ~TrainingPathManager();
	virtual PathManager* clone();
	virtual bool merge(const PathManager &pm);
	virtual bool less(const PathManager &b) const;
	virtual bool try_branch(bool direction, klee::Solver::Validity validity, 
			klee::KInstruction* inst, CVExecutionState *state);
	virtual void branch(bool direction, klee::Solver::Validity validity, 
			klee::KInstruction* inst, CVExecutionState *state);
	virtual void print(std::ostream &os) const;

	bool add_socket_event(const SocketEvent* se);
	void erase_socket_event(const SocketEvent* se);
	bool contains_socket_event(const SocketEvent* se);
	const SocketEventDataSet& socket_events() { return socket_events_; }

	void write(std::ostream &os);
	void read(std::ifstream &is);

 protected:
	explicit TrainingPathManager(const TrainingPathManager &pm);
	explicit TrainingPathManager(Path *path, PathRange &range, 
			SocketEventDataSet &socket_events); // only used by clone

	// Serialization
	friend class boost::serialization::access;
	template<class archive> 
		void serialize(archive & ar, const unsigned version);

	SocketEventDataSet socket_events_;
};

inline std::ostream &operator<<(std::ostream &os, 
		const TrainingPathManager &p) {
  p.print(os);
  return os;
}

////////////////////////////////////////////////////////////////////////////////

/// A VerifyPathManager instance is created by reading a TrainingPathManager
/// class instance from file.
class VerifyPathManager : public PathManager {
 public:
	VerifyPathManager(const Path *vpath, PathRange &vrange);
	virtual PathManager* clone();
	virtual bool less(const PathManager &b) const;
	virtual bool try_branch(bool direction, klee::Solver::Validity validity, 
			klee::KInstruction* inst, CVExecutionState *state);
	virtual void branch(bool direction, klee::Solver::Validity validity, 
			klee::KInstruction* inst, CVExecutionState *state);
	virtual void print(std::ostream &os) const;

	unsigned index() { return index_; }

 private:
	virtual bool merge(const PathManager &pm) { return false; }

 protected:
	explicit VerifyPathManager(const VerifyPathManager &pm);
	explicit VerifyPathManager();

	const Path *vpath_;
	PathRange vrange_;
	unsigned index_;
};

inline std::ostream &operator<<(std::ostream &os, 
		const VerifyPathManager &p) {
  p.print(os);
  return os;
}

////////////////////////////////////////////////////////////////////////////////

class VerifyConcretePathManager : public VerifyPathManager {
 public:
	VerifyConcretePathManager(const Path *vpath, PathRange &vrange);
	virtual PathManager* clone();
	virtual bool less(const PathManager &b) const;
	virtual bool try_branch(bool direction, klee::Solver::Validity validity, 
			klee::KInstruction* inst, CVExecutionState *state);
	virtual void branch(bool direction, klee::Solver::Validity validity, 
			klee::KInstruction* inst, CVExecutionState *state);

 private:
	virtual bool merge(const PathManager &pm) { return false; }

 protected:
	explicit VerifyConcretePathManager(const VerifyConcretePathManager &pm);
	explicit VerifyConcretePathManager();

	bool invalidated_;
};

inline std::ostream &operator<<(std::ostream &os, 
		const VerifyConcretePathManager &p) {
  p.print(os);
  return os;
}

////////////////////////////////////////////////////////////////////////////////

class VerifyPrefixPathManager : public VerifyPathManager {
 public:
	VerifyPrefixPathManager(const Path *vpath, PathRange &vrange);
	virtual PathManager* clone();
	virtual bool less(const PathManager &b) const;
	virtual bool try_branch(bool direction, klee::Solver::Validity validity, 
			klee::KInstruction* inst, CVExecutionState *state);
	virtual void branch(bool direction, klee::Solver::Validity validity, 
			klee::KInstruction* inst, CVExecutionState *state);

 private:
	virtual bool merge(const PathManager &pm) { return false; }

 protected:
	explicit VerifyPrefixPathManager(const VerifyPrefixPathManager &pm);
	explicit VerifyPrefixPathManager();

	bool invalidated_;
};

inline std::ostream &operator<<(std::ostream &os, 
		const VerifyPrefixPathManager &p) {
  p.print(os);
  return os;
}

////////////////////////////////////////////////////////////////////////////////

class StackDepthVerifyPathManager : public VerifyPathManager {
 public:
	StackDepthVerifyPathManager(const Path *vpath, PathRange &vrange);
	virtual PathManager* clone();
	virtual bool less(const PathManager &b) const;
	virtual bool try_branch(bool direction, klee::Solver::Validity validity, 
			klee::KInstruction* inst, CVExecutionState *state);
	virtual void branch(bool direction, klee::Solver::Validity validity, 
			klee::KInstruction* inst, CVExecutionState *state);

 private:
	virtual bool merge(const PathManager &pm) { return false; }

 protected:
	explicit StackDepthVerifyPathManager(const StackDepthVerifyPathManager &pm);
	explicit StackDepthVerifyPathManager();

};

inline std::ostream &operator<<(std::ostream &os, 
		const StackDepthVerifyPathManager &p) {
  p.print(os);
  return os;
}

////////////////////////////////////////////////////////////////////////////////

class HorizonPathManager : public VerifyPathManager {
 public:
	HorizonPathManager(const Path *vpath, PathRange &vrange);
	virtual PathManager* clone();
	virtual bool less(const PathManager &b) const;
	virtual bool try_branch(bool direction, klee::Solver::Validity validity, 
			klee::KInstruction* inst, CVExecutionState *state);
	virtual void branch(bool direction, klee::Solver::Validity validity, 
			klee::KInstruction* inst, CVExecutionState *state);

	void set_index(int index);
	bool is_horizon() { return is_horizon_; }

 private:
	virtual bool merge(const PathManager &pm) { return false; }

 protected:
	explicit HorizonPathManager(const VerifyConcretePathManager &pm);
	explicit HorizonPathManager();

	bool is_horizon_;
};

inline std::ostream &operator<<(std::ostream &os, 
		const HorizonPathManager &p) {
  p.print(os);
  return os;
}

////////////////////////////////////////////////////////////////////////////////

class PathManagerFactory {
 public:
  static PathManager* create();
};

struct PathManagerLT {
	bool operator()(const PathManager* a, const PathManager* b) const;
};

////////////////////////////////////////////////////////////////////////////////

class PathManagerSet {
 public:
	typedef std::set<PathManager*, PathManagerLT> set_ty;
	typedef set_ty::iterator iterator;
	typedef set_ty::const_iterator const_iterator;

	PathManagerSet();
	bool insert(PathManager* path);
	bool contains(PathManager* path);
	void erase(PathManager* path);
	PathManager* merge(PathManager* path);
	unsigned size() { return paths_.size(); }

	PathManagerSet::iterator begin() { return paths_.begin(); }
	PathManagerSet::iterator end() { return paths_.end(); }
	PathManagerSet::const_iterator begin() const { return paths_.begin(); }
	PathManagerSet::const_iterator end() const { return paths_.end(); }
 private: 
	set_ty paths_;
};

////////////////////////////////////////////////////////////////////////////////

} // end namespace cliver
#endif // CLIVER_PATH_MANAGER_H
