#ifndef PATH_SEGMENT_H
#define PATH_SEGMENT_H

#include "constraint_set.h"

#include "klee/ExprBuilder.h"
#include "klee/Solver.h"
#include "klee/Statistics.h"
#include "klee/Internal/Support/Timer.h"

#include <string>
#include <vector>
#include <set>
#include <deque>
#include <iostream>

#include "frame_stats.h"

namespace klee {

// Notes: 
// How do State Values change over time? When we add packetarrays, do they
// represent state at the end of a client loop or within a client loop?

//===----------------------------------------------------------------------===//

extern unsigned g_state_counter;

//===----------------------------------------------------------------------===//

// All candidate instantiated paths through a subsection of a client's loop,
// for some round.

class PathSeglet;
typedef std::vector< PathSeglet > PathSegletSet;
typedef std::vector< PathSeglet > PathSegletArray;
typedef std::vector< PathSegletSet > PathSegletSetChain;


// All candidate instantiated paths through a client's loop, for some round.
class PathSegment;
//typedef std::vector< PathSegment* > PathSegmentSet;
typedef std::vector< PathSegment* > PathSegmentSet;

//===----------------------------------------------------------------------===//

// The memory assignments for a given round. 

class StateData {
public:
  StateData();
  ~StateData();

  void add(ref<Expr> e);
  void add(Array* array);
  void add(shared_ptr<MemoryUpdate> mu) { updates_.add_new(mu); }
  int id() { return id_; }
  void addCommonUpdates(const PathSegmentSet &pss);
  void addCommonUpdates(const PathSegletSet &pss);

  shared_ptr<MemoryUpdate> find(const std::string name) const;

  bool addFromFile( const std::string &filename );
  const std::string& last_file_added() { return last_file_added_; }
  std::string getNameWithID(const std::string &name, int id);
  std::string getNameWithID(const std::string &name);

  const Array* rebuildArrayFromGeneric(const Array* array, StateData *previous);
  bool isRebuiltArray(const Array* array);
  std::set< std::string > getMarkedArrayNames() { return marked_array_names_; }
  void print(std::ostream &os) const;

private:
  int id_;
  std::string suffix_;
  MemoryUpdateSet updates_;
  std::string last_file_added_;
  std::map< std::string, const Array* > arrays_;
  std::set< const Array* > rebuilt_arrays_;
  std::vector< Array* > allocated_arrays_;
  std::set< std::string > marked_array_names_;
};

inline std::ostream &operator<<(std::ostream &os, 
                                const StateData& state_data) {
  state_data.print(os);
  return os;
}

//===----------------------------------------------------------------------===//

template<class T>
class ImpliedExprFinder: public ExprVisitor {
public:
  ImpliedExprFinder(T* pre_updates, T* post_updates= NULL) 
    : PreUpdates(pre_updates), PostUpdates(post_updates) {}

  ExprVisitor::Action visitRead(const ReadExpr &re) {
    ref<Expr> v = visit(re.index);
    if (re.updates.head != NULL)
      findImpliedExprs(re.updates, v);
    return Action::doChildren();
  }

  void findImpliedExprs(const UpdateList &ul, ref<Expr> index) {

    const Array* root = ul.root;

    for (const UpdateNode *un=ul.head; un; un=un->next) {
      ref<Expr> Index = visit(un->index);
      ref<Expr> Value = visit(un->value);
      ref<Expr> LH = ReadExpr::create(UpdateList(root, NULL), Index);
      ref<Expr> E = EqExpr::create(LH, Value);
      if (implied_set.insert(E).second)
        results.push_back(E);
    }
  }

  std::vector< ref<Expr> > results;

private:
  ExprHashSet implied_set;
  T *PreUpdates;
  T *PostUpdates;
};

#if 0
template<class T>
class ImpliedExprFinder: public ExprVisitor {
public:
  ImpliedExprFinder(T* pre_updates, T* post_updates= NULL) 
    : PreUpdates(pre_updates), PostUpdates(post_updates) {}

  ExprVisitor::Action visitRead(const ReadExpr &re) {
    ref<Expr> v = visit(re.index);
    if (re.updates.head != NULL)
      findImpliedExprs(re.updates, v);
    return Action::doChildren();
  }

  void findImpliedExprs(const UpdateList &ul, ref<Expr> index) {

    const Array* root = ul.root;
    shared_ptr<MemoryUpdate> Update, PreUpdate, PostUpdate;

    PreUpdate = PreUpdates->find(root->name);

    if (PostUpdates)
      PostUpdate = PostUpdates->find(root->name);
      
    assert(!PreUpdate.get() || !PostUpdate.get() 
           && "multiple MemoryUpdate matches");

    if (!PostUpdate.get())
      Update = PreUpdate;
    else
      Update = PostUpdate;
        
    if (Update.get()) {
      if (ConstantExpr *CE = dyn_cast<ConstantExpr>(index)) {
        uint8_t value;
        if (Update->find(CE->getZExtValue(), value)) {
          //llvm::cout << "Adding update to : " << ul.root->name << "\n";
          ref<Expr> LH = ReadExpr::create(UpdateList(root, NULL), index);
          ref<Expr> RH = ConstantExpr::create(value, Expr::Int8);
          ref<Expr> E = EqExpr::create(LH, RH);
          if (implied_set.insert(E).second)
            results.push_back(E);
        }
      } else {
        //llvm::cout << "Adding updates! to : " << ul.root->name << "\n";
        foreach (U, Update->begin(), Update->end()) {
          ref<Expr> Index = ConstantExpr::create(U->first, Expr::Int32);
          ref<Expr> LH = ReadExpr::create(UpdateList(root, NULL), Index);
          ref<Expr> RH = ConstantExpr::create(U->second, Expr::Int8);
          ref<Expr> E = EqExpr::create(LH, RH);
          if (implied_set.insert(E).second) 
            results.push_back(E);
        }
      }
    }
  }

  std::vector< ref<Expr> > results;

private:
  ExprHashSet implied_set;
  T *PreUpdates;
  T *PostUpdates;
};
#endif

//===----------------------------------------------------------------------===//

template<class T>
class ConcreteUpdateInserter: public ExprVisitor {
public:
  ConcreteUpdateInserter(T* pre_updates, T* post_updates= NULL) 
    : PreUpdates(pre_updates), PostUpdates(post_updates) {}

  ExprVisitor::Action visitRead(const ReadExpr &re) {
    ref<Expr> v = visit(re.index);

    if (re.updates.head != NULL)
      return insertUpdates(re.updates, v);
    
    if (v != re.index)
      return Action::changeTo(ReadExpr::create(re.updates, v));
    else
      return Action::doChildren();
  }

  ExprVisitor::Action insertUpdates(const UpdateList &ul, ref<Expr> index) {

    shared_ptr<MemoryUpdate> Update, PreUpdate, PostUpdate;

    PreUpdate = PreUpdates->find(ul.root->name);

    if (PostUpdates)
      PostUpdate = PostUpdates->find(ul.root->name);
      
    assert(!PreUpdate.get() || !PostUpdate.get() 
           && "multiple MemoryUpdate matches");

    if (!PostUpdate.get())
      Update = PreUpdate;
    else
      Update = PostUpdate;
        
    std::vector< const UpdateNode*> update_list;
    for (const UpdateNode *un=ul.head; un; un=un->next) 
      update_list.push_back(un);

    UpdateList updates(ul.root, NULL);
    //llvm::cout << "Attempting to add update to : " 
    //  << ul.root->name << " at index: " << index << "\n";
    //llvm::cout << "Searched: \n" << *PreUpdates 
    //  << "\nand Searched: \n" << *PostUpdates << "\n"; 

    if (Update.get()) {

      MemoryUpdate::ValueIndexMap concrete_values;
      if (ConstantExpr *CE = dyn_cast<ConstantExpr>(index)) {
        uint8_t value;
        unsigned i = CE->getZExtValue();
        if (Update->find(i, value)) {
          //llvm::cout << "Adding update  " << ul.root->name 
          //  << "[" << i << "] = " << (uint64_t)value << "\n";
          concrete_values[i] = value;
        }
      } else {
        //llvm::cout << "Adding updates! to : " << ul.root->name << "\n";
        concrete_values = MemoryUpdate::ValueIndexMap(Update->begin(), 
                                                      Update->end());
      }
      foreach (V, concrete_values.begin(), concrete_values.end()) {
        updates.extend(ConstantExpr::create(V->first, Expr::Int32),
                       ConstantExpr::create(V->second, Expr::Int8));
      }
    }

    foreach (U, update_list.rbegin(), update_list.rend()) {
      updates.extend(visit((*U)->index), visit((*U)->value));
    }

    return Action::changeTo(ReadExpr::create(updates, index));
  }

private:
  T *PreUpdates;
  T *PostUpdates;
};

#if 0
template<class T>
class ConcreteUpdateInserter: public ExprVisitor {
public:
  ConcreteUpdateInserter(T* pre_updates, T* post_updates= NULL) 
    : PreUpdates(pre_updates), PostUpdates(post_updates) {}

  //Action visitExpr(const Expr &e);
  //ref<Expr> getInitialValue(const Array& os, unsigned index);

  ExprVisitor::Action visitRead(const ReadExpr &re) {
    ref<Expr> v = visit(re.index);

    if (re.updates.head != NULL)
      return insertUpdates(re.updates, v);
    
    if (v != re.index)
      return Action::changeTo(ReadExpr::create(re.updates, v));
    else
      return Action::doChildren();
  }

  ExprVisitor::Action insertUpdates(const UpdateList &ul, ref<Expr> index) {

    shared_ptr<MemoryUpdate> Update, PreUpdate, PostUpdate;

    PreUpdate = PreUpdates->find(ul.root->name);

    if (PostUpdates)
      PostUpdate = PostUpdates->find(ul.root->name);
      
    assert(!PreUpdate.get() || !PostUpdate.get() 
           && "multiple MemoryUpdate matches");

    if (!PostUpdate.get())
      Update = PreUpdate;
    else
      Update = PostUpdate;
        
    std::vector< const UpdateNode*> update_list;
    for (const UpdateNode *un=ul.head; un; un=un->next) 
      update_list.push_back(un);

    UpdateList updates(ul.root, NULL);
    //llvm::cout << "Attempting to add update to : " << ul.root->name << "\n";
    //llvm::cout << "Searched: \n" << *PreUpdates << "\nand Searched: \n" << *PostUpdates << "\n"; 

    if (Update.get()) {

      MemoryUpdate::ValueIndexMap concrete_values;
      if (ConstantExpr *CE = dyn_cast<ConstantExpr>(index)) {
        uint8_t value;
        unsigned index = CE->getZExtValue();
        if (Update->find(index, value)) {
          //llvm::cout << "Adding update to : " << ul.root->name << "\n";
          concrete_values[index] = value;
        }
      } else {
        //llvm::cout << "Adding updates! to : " << ul.root->name << "\n";
        concrete_values = MemoryUpdate::ValueIndexMap(Update->begin(), 
                                                      Update->end());
      }
      foreach (V, concrete_values.begin(), concrete_values.end()) {
        updates.extend(ConstantExpr::create(V->first, Expr::Int32),
                       ConstantExpr::create(V->second, Expr::Int8));
      }
    }

    foreach (U, update_list.rbegin(), update_list.rend()) {
      updates.extend(visit((*U)->index), visit((*U)->value));
    }

    return Action::changeTo(ReadExpr::create(updates, index));
    // could continue to do work ...
  }

private:
  T *PreUpdates;
  T *PostUpdates;
};
#endif

//===----------------------------------------------------------------------===//

class RenameVisitor: public ExprVisitor {
private:
  StateData *state_previous_;
  StateData *state_;

public:
  RenameVisitor(StateData *state_previous, StateData *state) 
    : ExprVisitor(true),
      state_previous_(state_previous), state_(state) {}

  Action visitRead(const ReadExpr &e) {
    //llvm::cout << "visiting: " << e.updates.root->name << "\n";
    if (state_->isRebuiltArray(e.updates.root) 
        || state_previous_->isRebuiltArray(e.updates.root))  {
      //llvm::cout << "not changing : " << e.updates.root->name << "\n";
      return Action::doChildren();
    }

    const Array *array 
      = state_->rebuildArrayFromGeneric(e.updates.root, state_previous_);

    // Because extend() pushes a new UpdateNode onto the list, we need to walk
    // the list in reverse to rebuild it in the same order.
    std::vector< const UpdateNode*> update_list;
    for (const UpdateNode *un=e.updates.head; un; un=un->next) {
      update_list.push_back(un);
    }
    // walk list in reverse
    UpdateList updates(array, NULL);
    foreach (U, update_list.rbegin(), update_list.rend()) {
      updates.extend(visit((*U)->index), visit((*U)->value));
    }

    return Action::changeTo(ReadExpr::create(updates, e.index));
  }
};

//===----------------------------------------------------------------------===//

// TBD
class GenericConstraint {

public:
  GenericConstraint( std::string &filename );
  ref<Expr> generate( StateData *pre, StateData *post );
  std::string filename() { return filename_; }
private:
  std::string filename_;
  std::string suffix_;
  ref<Expr> constraint_;
  std::vector< ref<Expr> > constraints_;
  // add a list of symbolics?
};

//===----------------------------------------------------------------------===//

// An instantiated KLEE path through a subsection of a client's main loop.

class PathSeglet {
public:
  PathSeglet(StateData* pre, StateData* post, GenericConstraint* gc);
  PathSeglet(ref<Expr> e);

  bool valid() { return family_.valid(); }
  ref<Expr> constraint() { return constraint_; }

  const ConstraintSetFamily& family() const { return family_; }
  const ConstraintSetFamily& constraints() const { return family_; }
  const std::string& id() const { return id_; }
  void set_id(const std::string id) { id_ = id; }
  void print(std::ostream &os) const;
  const MemoryUpdateSet& getImpliedUpdates() const { return family_.getImpliedUpdates(); }
  bool checkImpliedValues();

private:
  PathSeglet() {};
  StateData* pre_state_;
  StateData* post_state_;
  ref<Expr> constraint_;
  std::string id_;
  ConstraintSetFamily family_;
  uint64_t num_id_;
};

inline std::ostream &operator<<(std::ostream &os, 
                                const PathSeglet& seglet) {
  seglet.print(os);
  return os;
}

//===----------------------------------------------------------------------===//
// A collection of PathSeglets which constitute a candidiate PathSegment for a
// give client round.

class PathSegment {
public:
  PathSegment(const PathSegment &path_segment);
  PathSegment(const PathSegletArray &seglets);
  PathSegment(const PathSeglet &seglet);
  //~PathSegment();

  PathSegment &operator=(const PathSegment &ps);
  void add(const PathSeglet &seglet);
  bool extend(const PathSegment &segment);
  bool valid() { return constraints_.valid(); }
  bool query(Solver* solver, StateData* state_data, std::ostream &os);
  bool equals(const PathSegment &ps) const;
  std::string id();
  //const std::string& id_recent() { return id_vec_.back(); }

  void set_mark(bool mark) { constraints_.set_mark(mark); }
  bool checkForImpliedUpdates() { return constraints_.checkForImpliedUpdates(); }
  
  const ConstraintSetFamily& constraints() const { return constraints_; }
  //const PathSegletArray& seglets() { return seglets_; }
  
private:
  PathSegment() {}
  std::vector<std::string> id_vec_;
  //PathSegletArray seglets_;
  ConstraintSetFamily constraints_;
  uint64_t num_id_;
};

//===----------------------------------------------------------------------===//
#if 0
//===----------------------------------------------------------------------===//


// TBD
class PathSegmentChain;
typedef std::vector< PathSegmentChain > PathSegmentChainSet;

//===----------------------------------------------------------------------===//

// TBD

class PathSegmentChain {
public:
  PathSegmentChain();
  bool add(const PathSegment &path_segment);
  bool query();

private:
  ConstraintSetFamily constraints_;
  //std::vector< PathSeglet > seglets_;
};

//===----------------------------------------------------------------------===//
#endif

// TBD

class PathManager {
public:
  PathManager();
  void init(std::string initial_state);
  void reset_solver();
  void extendChains( const PathSegletSetChain &seglet_set_chain );
  int queryChains();
  void addStateData(std::string filename);
  void printCommonUpdates();

  StateData* previous();
  StateData* current();

  PathSegmentSet* makeSegments(const PathSegletSetChain &chain);

  void makeSegmentsHelper(PathSegmentSet &segments, 
                          const PathSegletArray &path,
                          const PathSegletSetChain &chain,
                          unsigned index );

private:
  PathSegmentSet *chains_;
  std::deque<StateData*> states_;
  Solver* solver_;
};

//===----------------------------------------------------------------------===//


} // End namespace klee

#endif // PATH_SEGMENT_H
