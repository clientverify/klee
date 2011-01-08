#include "memory_update.h"

using namespace klee;

//////////////////////////////////////////////////////////////////////////////
//class MemoryUpdate

MemoryUpdate::MemoryUpdate(const Array* array)
    : array_(const_cast<Array*>(array)), 
      name_(array->name), size_(array->size) 
{
  // constantValues is a vector< ref<ConstantExpr> >, each element should be
  // 8bits wide. We iterate over each value and add it to values_.
  int index = 0;
  foreach (it, array->constantValues.begin(), array->constantValues.end()) {
    ref<ConstantExpr> CE = *it;
    if (CE->getWidth() != 8) llvm::cout << name_ << "[" << index << "] width = " << CE->getWidth() << "\n";
    // getZExtValue asserts that CE->width() <= 8
    add(index, (uint8_t)CE->getZExtValue(8));
    index++;
  }
}

MemoryUpdate::~MemoryUpdate() {
  //delete memory_object_;
}

void MemoryUpdate::add(unsigned index, uint8_t value) {
  values_[index] = value;
}

bool MemoryUpdate::find(unsigned index, uint8_t &result) {
  let(it, values_.find(index));
  if (it != values_.end()) {
    result = it->second;
    return true;
  }
  return false;
}

bool MemoryUpdate::testForConflict(const MemoryUpdate& mu) {
  foreach(v, mu.begin(), mu.end()) {
    uint8_t val;
    if (find(v->first, val)) {
      if (v->second != val) {
        //DEBUG(llvm::cout << "Conflict: " << array_->name << "[" << v->first << "] = ");
        //DEBUG(llvm::cout << v->second << " or " << val "\n");
        return true;
      }
    }
  }
  return false;
}

void MemoryUpdate::print(std::ostream &os) const {
  if (array_) {
    os << array_->name;
  } else {
    os << "array";
  }
  os << " =";
  std::vector<unsigned> vals;
  int start = -1;
  int current = 0;
  foreach(it, values_.begin(), values_.end()) {
    if (start == -1) {
      current = it->first - 1;
      start = it->first;
    }
    if (abs((int)(it->first) - current) == 1) {
      current = it->first;
      vals.push_back(it->second);
    } else {
      bool first = true;
      os << " [" << start << ":" << current << "]=";
      os << std::hex;
      foreach(v, vals.begin(), vals.end()) {
        if (first) {
          os << *v;
          first = false;
        } else {
          os << ":" << *v;
        }
      }
      os << std::dec;
      start = it->first;
      current = it->first;
      vals.clear();
      vals.push_back(it->second);
    }
  }

  if (vals.size() > 0) {
    bool first = true;
    os << " [" << start << ":" << current << "]=";
    os << std::hex;
    foreach(v, vals.begin(), vals.end()) {
      if (first) {
        os << *v;
        first = false;
      } else {
        os << ":" << *v;
      }
    }
    os << std::dec;
  }
}

bool MemoryUpdate::equals(const MemoryUpdate &b) const {
  // check name and # of updates 
  if ((name_ != b.name_) || (values_.size() != b.values_.size()))
    return false;

  foreach(v, values_.begin(), values_.end()) {
    let(b_v, b.values_.find(v->first));
    if ((b_v == b.values_.end()) || (b_v->second != v->second)) {
      return false;
    }
  }
  return true;
}

//////////////////////////////////////////////////////////////////////////////
//class MemoryUpdateSet

MemoryUpdateSet::MemoryUpdateSet(std::vector<shared_ptr<MemoryUpdate> > update_vec) {
  foreach(it, update_vec.begin(), update_vec.end()) {
    add_new(*it);
  }
}

MemoryUpdateSet::MemoryUpdateSet(shared_ptr<MemoryUpdate> update) {
    add_new(update);
}

void MemoryUpdateSet::add_new(shared_ptr<MemoryUpdate> mu) {
  updates_[mu->name()] = mu;
}

shared_ptr<MemoryUpdate> MemoryUpdateSet::find(const std::string name) const {
  shared_ptr<MemoryUpdate> result;
  let(it, updates_.find(name));
  if (it != updates_.end()) {
    result = it->second;
  } 
  return result;
}

void MemoryUpdateSet::add(const Array *array,
                          const std::string name, unsigned index, unsigned value) {
  let(it, updates_.find(name));
  if(it != updates_.end()) {
    assert(array == it->second->array() && "array pointers are not equal");
    it->second->add(index, (uint8_t)value);
  } else {
    shared_ptr<MemoryUpdate> mu(new MemoryUpdate(array));
    updates_[mu->name()] = mu;
    mu->add(index, (uint8_t)value);
  }
}

void MemoryUpdateSet::add(const std::string name, unsigned index, unsigned value) {
  let(it, updates_.find(name));
  if(it != updates_.end()) {
    it->second->add(index, (uint8_t)value);
  } else {
    //cerr << "MemoryUpdateSet::add() error" << endl;
  }
}

void MemoryUpdateSet::print(std::ostream &os) const {
  foreach(it, updates_.begin(), updates_.end()) {
    if (it != updates_.begin()) os << std::endl;
    os << *(it->second);
  }
}

bool MemoryUpdateSet::equals(const MemoryUpdateSet &b) const {
  if (updates_.size() != b.updates_.size())
    return false;

  foreach(mu, updates_.begin(), updates_.end()) {
    let(b_mu, b.updates_.find(mu->first));
    if ((b_mu == b.updates_.end()) 
      || !(mu->second->equals(*(b_mu->second)))) {
      return false;
    } 
  }
  return true;
}

