#ifndef MEMORY_UPDATE_H
#define MEMORY_UPDATE_H

#include "klee/Expr.h"
#include "klee/Core/Memory.h"

#include <vector>
#include <string>
#include <map>
#include <boost/shared_ptr.hpp>
#ifdef CV_USE_HASH
#include <ext/hash_map>
#include <ext/hash_set>
#endif

#define let(_lhs, _rhs)  typeof(_rhs) _lhs = _rhs

#define foreach(_i, _b, _e) \
	  for(typeof(_b) _i = _b, _i ## end = _e; _i != _i ## end;  ++ _i)

using boost::shared_ptr;

namespace klee {

class MemoryUpdate {
public:
  MemoryUpdate() : array_(NULL), name_(""), size_(0) {};

  MemoryUpdate(const Array* array);
  /*
  MemoryUpdate(const Array* array)
    : array_(const_cast<Array*>(array)), 
      name_(array->name), size_(array->size) {}
      */

  ~MemoryUpdate();

  void add(unsigned index, uint8_t value);
  bool find(unsigned index, uint8_t &result);
  bool testForConflict(const MemoryUpdate& mu);

  std::string name() { return name_; }
  size_t    values_size() { return values_.size(); }
  size_t    size() { return size_; }
  Array*    array() const { return array_; }

  void print(std::ostream &os) const;
  bool equals(const MemoryUpdate &b) const;

#ifdef CV_USE_HASH
  typedef __gnu_cxx::hash_map<unsigned, uint8_t> ValueIndexMap;
#else
  typedef std::map<unsigned, uint8_t> ValueIndexMap;
#endif

  ValueIndexMap::const_iterator begin() const { return values_.begin(); }
  ValueIndexMap::const_iterator end() const { return values_.end(); }

private:
  Array* array_;
  std::string name_;
  unsigned size_;
  ValueIndexMap values_;

};

inline std::ostream &operator<<(std::ostream &os, const MemoryUpdate &mu) {
  mu.print(os);
  return os;
}

class MemoryUpdateSet {
public:
  MemoryUpdateSet() {}
  MemoryUpdateSet(std::vector< shared_ptr<MemoryUpdate> > update_vec);
  MemoryUpdateSet(shared_ptr<MemoryUpdate>  update);

  void add_new(shared_ptr<MemoryUpdate> mu);
  void add(std::string name, unsigned index, unsigned value);
  void add(const Array* array, std::string name, unsigned index, unsigned value);

  shared_ptr<MemoryUpdate> find(std::string name) const;
  bool equals(const MemoryUpdateSet &us) const;
  void print(std::ostream &os) const;
  size_t size() const { return updates_.size(); }

  typedef std::map<std::string, shared_ptr<MemoryUpdate> > MemoryUpdateIdMap;

  MemoryUpdateIdMap::const_iterator begin() const { 
    return updates_.begin();
  }
  MemoryUpdateIdMap::const_iterator end() const { 
    return updates_.end();
  }

private:
  MemoryUpdateIdMap updates_;
};

inline std::ostream &operator<<(std::ostream &os, const MemoryUpdateSet &us) {
  us.print(os);
  return os;
}

} // end namespace klee

#endif
