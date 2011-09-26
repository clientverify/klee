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

//class PathManager {
//public:
// virtual PathManager* clone() = 0;
// virtual bool merge(const PathManager &pm);
// virtual bool less(const PathManager &b) const;
//
// virtual bool query_branch(bool direction, klee::KInstruction* inst);
// virtual bool commit_branch(bool direction, klee::KInstruction* inst);
//private:
//}

////////////////////////////////////////////////////////////////////////////////

class SocketEvent;

/// PathManager is a wrapper around a single Path that allows for additional
/// information to be associated with a Path. PathManager interface is used by
/// CVExecutor to query whether a particular branch is feasible and to indicate
/// when a particular branch is followed. Every CVExecutionState holds a single
/// PathManager which is cloned when a CVExecutionState is cloned.

class PathManager {
 public:
	PathManager();
	virtual PathManager* clone();
	virtual bool merge(const PathManager &pm);
	virtual bool less(const PathManager &b) const;
	virtual bool query_branch(bool direction, klee::KInstruction* inst);
	virtual bool commit_branch(bool direction, klee::KInstruction* inst);
	virtual void print(std::ostream &os) const;

	void set_path(Path* path);
	void set_range(const PathRange& range);

	Path* path() { return path_; }
	unsigned length() { return path_->length(); }
	PathRange range() { return range_; }

 protected:
	explicit PathManager(const PathManager &pm);
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
	typedef std::set<SocketEvent*> message_set_ty;
	TrainingPathManager();
	virtual PathManager* clone();
	virtual bool merge(const PathManager &pm);
	virtual bool less(const PathManager &b) const;
	virtual bool query_branch(bool direction, klee::KInstruction* inst);
	virtual bool commit_branch(bool direction, klee::KInstruction* inst);
	virtual void print(std::ostream &os) const;

	bool add_message(const SocketEvent* se);
	bool contains_message(const SocketEvent* se);
	const message_set_ty& messages() { return messages_; }

	void write(std::ostream &os);
	void read(std::ifstream &is);

 protected:
	explicit TrainingPathManager(const TrainingPathManager &pm);

	// Serialization
	friend class boost::serialization::access;
	template<class archive> 
		void serialize(archive & ar, const unsigned version);

	message_set_ty messages_;
};

inline std::ostream &operator<<(std::ostream &os, 
		const TrainingPathManager &p) {
  p.print(os);
  return os;
}

////////////////////////////////////////////////////////////////////////////////

/// A VerifyPathManager instance is created by reading a TrainingPathManager
/// class instance from file.
class VerifyPathManager : public TrainingPathManager {
 public:
	VerifyPathManager();
	virtual PathManager* clone();
	virtual bool merge(const PathManager &pm);
	virtual bool less(const PathManager &b) const;
	virtual bool query_branch(bool direction, klee::KInstruction* inst);
	virtual bool commit_branch(bool direction, klee::KInstruction* inst);
	virtual void print(std::ostream &os) const;

	unsigned index() { return index_; }

 protected:
	explicit VerifyPathManager(const VerifyPathManager &pm);

	unsigned index_;
};

inline std::ostream &operator<<(std::ostream &os, 
		const VerifyPathManager &p) {
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
	bool add(PathManager* path);
	bool contains(PathManager* path);
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

class PathSelector {
 public:
	PathSelector();
	PathSelector(PathManagerSet* paths);
	PathManager* next_path();
 private:
	unsigned index_;
	std::vector<PathManager*> paths_;
};

class PathSelectorFactory {
 public:
  static PathSelector* create();
};


} // end namespace cliver
#endif // CLIVER_PATH_MANAGER_H
