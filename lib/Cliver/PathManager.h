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
/// information to be associated with a Path, such as the PathRange and 
/// socket messages that were sent or received at the termination of the Path
class PathManager {
 public:
	typedef std::set<SocketEvent*> message_set_ty;
	PathManager();
	virtual PathManager* clone();
	virtual bool merge(const PathManager &pm);
	virtual bool less(const PathManager &b) const;
	virtual bool query_branch(bool direction, klee::KInstruction* inst);
	virtual bool commit_branch(bool direction, klee::KInstruction* inst);

	bool add_message(const SocketEvent* se);
	bool contains_message(const SocketEvent* se);
	void set_range(const PathRange& range);
	void set_path(Path* path);

	Path* path() { return path_; }
	unsigned length() { return path_->length(); }
	PathRange range() { return range_; }
	const message_set_ty& messages() { return messages_; }

	void write(std::ostream &os);
	void read(std::ifstream &is);
	void print(std::ostream &os) const;

 protected:
	explicit PathManager(const PathManager &pm);

	// Serialization
	friend class boost::serialization::access;
	template<class archive> 
		void serialize(archive & ar, const unsigned version);

	Path* path_;
	PathRange range_;
	message_set_ty messages_;
};

inline std::ostream &operator<<(std::ostream &os, 
		const PathManager &p) {
  p.print(os);
  return os;
}

////////////////////////////////////////////////////////////////////////////////

/// A VerifyPathManager instance is created by reading a recorded path
/// from file. When ::add_branch() is called, this class returns true if 
/// the added branch value matches the stored branch value. This functionality
/// is used in CVExecutor::fork() where a failed add_branch while result in the
/// state's termination.
class VerifyPathManager : public PathManager {
 public:
	VerifyPathManager();
	virtual PathManager* clone();
	virtual bool merge(const PathManager &pm);
	virtual bool less(const PathManager &b) const;
	virtual bool query_branch(bool direction, klee::KInstruction* inst);
	virtual bool commit_branch(bool direction, klee::KInstruction* inst);

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

class PathSet {
 public:
	typedef std::set<PathManager*, PathManagerLT> set_ty;
	typedef set_ty::iterator iterator;
	typedef set_ty::const_iterator const_iterator;

	PathSet();
	bool add(PathManager* path);
	bool contains(PathManager* path);
	PathManager* merge(PathManager* path);
	unsigned size() { return paths_.size(); }

	PathSet::iterator begin() { return paths_.begin(); }
	PathSet::iterator end() { return paths_.end(); }
	PathSet::const_iterator begin() const { return paths_.begin(); }
	PathSet::const_iterator end() const { return paths_.end(); }
 private: 
	set_ty paths_;
};

} // end namespace cliver
#endif // CLIVER_PATH_MANAGER_H
