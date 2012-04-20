//===-- ExecutionTrace.h ----------------------------------------*- C++ -*-===//
//
// <insert license>
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//
#ifndef CLIVER_EXECUTION_TRACE_H
#define CLIVER_EXECUTION_TRACE_H

#include <vector>
#include "cliver/ExecutionTraceTree.h"

#if 0
namespace boost { void throw_exception(std::exception const& e); }

#include <boost/serialization/access.hpp>
#include <boost/serialization/vector.hpp>

namespace llvm {
	class BasicBlock;
}

namespace cliver {

////////////////////////////////////////////////////////////////////////////////
 
/// Stores list of BasicBlocks for a single run of execution
class ExecutionTrace {
 public:
  typedef uint16_t BasicBlockID;
  typedef BasicBlockID ID;

  typedef std::vector<BasicBlockID> BasicBlockList;

  typedef BasicBlockList::iterator iterator;
  typedef BasicBlockList::const_iterator const_iterator;

  ExecutionTrace() {}
  ExecutionTrace(BasicBlockID bb) { this->push_back(bb); }
  ExecutionTrace(iterator start, iterator end) : basic_blocks_(start, end) {}

  void push_back(BasicBlockID kbb) { 
    basic_blocks_.push_back(kbb);
  }

  void push_front(BasicBlockID kbb) { 
    basic_blocks_.insert(basic_blocks_.begin(), kbb);
  }

  void push_back(const ExecutionTrace& etrace);
  void push_front(const ExecutionTrace& etrace);

  iterator begin() { return basic_blocks_.begin(); }
  iterator end() { return basic_blocks_.end(); }
  const_iterator begin() const { return basic_blocks_.begin(); }
  const_iterator end() const { return basic_blocks_.end(); }

  inline BasicBlockID& operator[](unsigned i) { return basic_blocks_[i]; }
  inline const BasicBlockID& operator[](unsigned i) const { return basic_blocks_[i]; }

  bool operator==(const ExecutionTrace& b) const;
  bool operator!=(const ExecutionTrace& b) const;
  bool operator<(const ExecutionTrace& b) const;

  inline size_t size() const { return basic_blocks_.size(); } 

  void insert(iterator position, iterator first, iterator last) {
    basic_blocks_.insert(position, first, last);
  }

  void insert(iterator position, BasicBlockID kbb) { 
    basic_blocks_.insert(position, kbb);
  }

  void erase(iterator first, iterator last) {
    basic_blocks_.erase(first, last);
  }

 protected:
  friend class boost::serialization::access;
  template<class Archive>
  void save(Archive & ar, const unsigned int version) const {
    ar & basic_blocks_;
  }

  template<class Archive>
  void load(Archive & ar, const unsigned int version) {
    ar & basic_blocks_;
  }

  BOOST_SERIALIZATION_SPLIT_MEMBER()

 private:
  BasicBlockList basic_blocks_;
};

std::ostream& operator<<(std::ostream& os, const ExecutionTrace &etrace);

////////////////////////////////////////////////////////////////////////////////

struct ExecutionTraceLT{
	bool operator()(const ExecutionTrace* a, const ExecutionTrace* b) const {
    return *(a) < *(b);
  }
};

////////////////////////////////////////////////////////////////////////////////


} // end namespace cliver

#endif

#endif // CLIVER_EXECUTION_TRACE_H

