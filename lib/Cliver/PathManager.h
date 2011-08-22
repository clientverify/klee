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

#include "klee/Internal/Module/InstructionInfoTable.h"
#include "klee/Internal/Module/KInstruction.h"
#include "ClientVerifier.h"

#include <list>
#include <set>
#include <fstream>
#include <iostream>

#include <boost/serialization/access.hpp>
#include <boost/serialization/vector.hpp>
#include <boost/archive/text_oarchive.hpp>
#include <boost/archive/text_iarchive.hpp>

namespace cliver {

////////////////////////////////////////////////////////////////////////////////

class PathRange {
 public:
	typedef std::pair<klee::KInstruction*, klee::KInstruction*> kinst_pair_ty;
	typedef std::pair<llvm::Instruction*, llvm::Instruction*> inst_pair_ty;
	typedef std::pair<unsigned, unsigned> inst_id_pair_ty;

	PathRange(klee::KInstruction* s, klee::KInstruction* e);
	PathRange();
	bool less(const PathRange &b) const;
	bool equal(const PathRange &b) const;
	int compare(const PathRange &b) const;

	klee::KInstruction* start() { return start_; }
	klee::KInstruction* end() { return end_; }

	inst_id_pair_ty ids() const;
	inst_pair_ty 		insts() const;
	kinst_pair_ty 	kinsts() const;

	void print(std::ostream &os) const;

 private:
	// Serialization
	friend class boost::serialization::access;
	template<class archive> 
		void save(archive & ar, const unsigned version) const;
	template<class archive> 
		void load(archive & ar, const unsigned version);
	BOOST_SERIALIZATION_SPLIT_MEMBER()

	klee::KInstruction* start_;
	klee::KInstruction* end_;
};

inline std::ostream &operator<<(std::ostream &os, 
		const PathRange &pr) {
  pr.print(os);
  return os;
}

////////////////////////////////////////////////////////////////////////////////

class Path {
 public:
	Path();
	Path(Path* parent);
	~Path();
	unsigned length() const;
	void add(bool direction, klee::KInstruction* inst);
	bool less(const Path &b) const;
	bool equal(const Path &b) const;
	void inc_ref();
	void dec_ref();
	bool get_branch(int index);
	klee::KInstruction* get_kinst(int index);
	void print(std::ostream &os) const;

 private: 
	explicit Path(const Path &p);

	// Helper functions
	void consolidate_branches(std::vector<bool> &branches) const;

	// Serialization
	friend class boost::serialization::access;
	template<class archive> 
		void save(archive & ar, const unsigned version) const;
	template<class archive> 
		void load(archive & ar, const unsigned version);
	BOOST_SERIALIZATION_SPLIT_MEMBER()

	// Member Variables
	const Path *parent_;
	mutable unsigned ref_count_;
    int length_;

	std::vector<bool> branches_;
	std::vector<klee::KInstruction*> instructions_;
};

inline std::ostream &operator<<(std::ostream &os, const Path &p) {
  p.print(os);
  return os;
}
 
////////////////////////////////////////////////////////////////////////////////

class SocketEvent;

class PathManager {
 public:
	typedef std::set<SocketEvent*> message_set_ty;
	PathManager();
	virtual PathManager* clone();
	virtual bool merge(const PathManager &pm);
	virtual bool less(const PathManager &b) const;
	virtual void add_branch(bool direction, klee::KInstruction* inst);

	bool add_message(const SocketEvent* se);
	void set_range(const PathRange& range);

	unsigned length() { return path_->length(); }
	PathRange range() { return range_; }
	const message_set_ty& messages() { return messages_; }

	void add_false_branch(klee::KInstruction* inst);
	void add_true_branch(klee::KInstruction* inst);
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

class VerifyPathManager : public PathManager {
 public:
	VerifyPathManager() {}
	VerifyPathManager(Path* path);
	//**virtual PathManager* clone();
	//**virtual bool merge(const VerifyPathManager &pm);
	//**virtual bool less(const VerifyPathManager &b) const;
	virtual void add_branch(bool direction, klee::KInstruction* inst);
	//bool add_message(const SocketEvent* se);
	//void set_range(const PathRange& range);
	
	//unsigned length() { return path_->length(); }
	//PathRange range() { return range_; }
	//const message_set_ty& messages() { return messages_; }

	//void add_false_branch(klee::KInstruction* inst);
	//void add_true_branch(klee::KInstruction* inst);
	//void write(std::ostream &os);
	//void read(std::ifstream &is);
	//**void print(std::ostream &os) const;

 protected:
	explicit VerifyPathManager(const VerifyPathManager &pm);

	//// Serialization
	//friend class boost::serialization::access;
	//template<class archive> 
	//	void serialize(archive & ar, const unsigned version);

	//Path* path_;
	//PathRange range_;
	//message_set_ty messages_;

	int index_;
	bool valid_;

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
