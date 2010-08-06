#include "path_segment.h"

#include "expr/Lexer.h"
#include "expr/Parser.h"

//#define DEBUG_TYPE "path-segment"
#define DEBUG_TYPE "xpilot-checker"

#include "klee/Constraints.h"

#include "llvm/ADT/OwningPtr.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Streams.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/System/Signals.h"

using namespace klee;
using namespace llvm;
using namespace klee::expr;

// Global id counter used to identify StateData objects
uint64_t g_state_counter = 0;
uint64_t g_seglet_id = 0;
uint64_t g_segment_id = 0;

FrameStats klee::fstats;

// Used to rename variables in the log files.
std::map<std::string,std::string> *g_array_names;
namespace {

  cl::opt<bool>
  UseDummySolver("use-dummy-solver",
		   cl::init(false));

  cl::opt<int>
  StateDataCount("state-data-count",
                  cl::desc("max # of state data objs to maintain; "
                           "small value may cause memory errors. The default (0) "
                           "maintains all state data objs"),
		   cl::init(0));

  cl::opt<bool>
  UseFastCexSolver("use-fast-cex-solver",
		   cl::init(false));
 
  cl::opt<bool>
  UseCachingSolver("use-caching-solver",
		   cl::init(true));
 
  cl::opt<bool>
  UseForkedSTP("use-forked-stp",
		   cl::init(false));
  
  cl::opt<bool>
  UseSTPQueryPCLog("use-stp-query-pc-log",
                   cl::init(false));
  cl::opt<bool>
  PrintQueries("print-queries",
                   cl::desc("Print solver queries and counterexamples."),
                   cl::init(false));

  cl::opt<bool>
  PrintChains("print-chains",
                   cl::desc("Print valid chains each frame."),
                   cl::init(false));

  cl::opt<bool>
  UseFileIds("use-file-ids",
             cl::desc("Use list of files as id for each path."),
             cl::init(false));

  cl::opt<bool>
  RemoveUpdates("remove-updates",
		   cl::init(false));

  cl::opt<bool>
  InitialStateIsLog("initial-state-is-log",
		   cl::init(false));

  cl::opt<bool>
  CommonUpdates("common-updates",
		   cl::init(true));




template<class T>
ref<Expr> exprFromVec( T begin, T end, Expr::Kind kind) {
  ref<Expr> res;
  foreach(it, begin, end) {
    if (res.get()) {
      switch(kind) {
      case Expr::And:
        res = AndExpr::create(res, *it);
        break;
      case Expr::Or:
        res = OrExpr::create(res, *it);
        break;
      default:
        assert(false && "invalid type");
      }
    } else {
      res = *it;
    }
  }
  return res;
}
}

//===----------------------------------------------------------------------===//
#if 0
template<class T>
ExprVisitor::Action ConcreteUpdateInserter::visitExpr(const Expr &e) {
  // Evaluate all constant expressions here, in case they weren't folded in
  // construction. Don't do this for reads though, because we want them to go to
  // the normal rewrite path.
  unsigned N = e.getNumKids();
  if (!N || isa<ReadExpr>(e))
    return Action::doChildren();

  for (unsigned i = 0; i != N; ++i)
    if (!isa<ConstantExpr>(e.getKid(i)))
      return Action::doChildren();

  ref<Expr> Kids[3];
  for (unsigned i = 0; i != N; ++i) {
    assert(i < 3);
    Kids[i] = e.getKid(i);
  }

  return Action::changeTo(e.rebuild(Kids));
}
#endif
//===----------------------------------------------------------------------===//

static bool getConstantArraysFromFile(std::string filename, 
                                 std::vector< const Array* > &constantArrays) {
  // Read pc file into MemoryBuffer
  std::string ErrorStr;
  MemoryBuffer *MB = MemoryBuffer::getFile(filename.c_str(), &ErrorStr);
  if (!MB) {
    llvm::cout << "getConstantArraysFromFile error on (" 
      << filename << "): " << ErrorStr << "\n";
    return false;
  }

  // Create Parser
  ExprBuilder *Builder = createDefaultExprBuilder();
  Parser *P = Parser::Create(filename, MB, Builder);
  P->SetMaxErrors(20);

  std::vector<Decl*> Decls;
  while (Decl *D = P->ParseTopLevelDecl()) {
    // Only read constant ArrayDecl
    if (ArrayDecl *AD = dyn_cast<ArrayDecl>(D)) {
      if (AD->Root->isConstantArray()) {
        constantArrays.push_back(AD->Root);
      }
    }
    Decls.push_back(D);
  }

  bool success = true;
  if (unsigned N = P->GetNumErrors()) {
    llvm::cerr << filename << ": parse failure: "
               << N << " errors.\n";
    success = false;
  }

  for (std::vector<Decl*>::iterator it = Decls.begin(),
         ie = Decls.end(); it != ie; ++it)
    delete *it;

  delete P;

  return success;
}

static bool getConstraintsFromFile(std::string filename, 
                                   std::vector< ExprHandle > &Constraints) {
  // Read pc file into MemoryBuffer
  std::string ErrorStr;
  MemoryBuffer *MB = MemoryBuffer::getFile(filename.c_str(), &ErrorStr);
  if (!MB) {
    llvm::cerr << "getConstraintsFromFile error on (" 
      << filename << "): " << ErrorStr << "\n";
    return false;
  }

  // Create Parser
  ExprBuilder *Builder = createDefaultExprBuilder();
  Parser *P = Parser::Create(filename, MB, Builder);
  P->SetMaxErrors(20);

  std::vector<Decl*> Decls;
  while (Decl *D = P->ParseTopLevelDecl()) {
    Decls.push_back(D);
  }

  bool success = true;
  if (unsigned N = P->GetNumErrors()) {
    llvm::cerr << filename << ": parse failure: "
               << N << " errors.\n";
    success = false;
  }

  for (std::vector<Decl*>::iterator it = Decls.begin(),
         ie = Decls.end(); it != ie; ++it) {
    Decl *D = *it;
    // Copy Constraints from the QueryCommand
    if (QueryCommand *QC = dyn_cast<QueryCommand>(D)) {
      Constraints = QC->Constraints;
    }
  }

  for (std::vector<Decl*>::iterator it = Decls.begin(),
         ie = Decls.end(); it != ie; ++it)
    delete *it;

  delete P;

  return success;
}

//===----------------------------------------------------------------------===//

// class StateData

StateData::StateData()
  : id_(++::g_state_counter), suffix_("__COPY__") {}

StateData::~StateData() {
  foreach (array, allocated_arrays_.begin(), allocated_arrays_.end())
    delete *array;
}

std::string StateData::getNameWithID(const std::string &name) {
  return getNameWithID(name, id_);
}

std::string StateData::getNameWithID(const std::string &name, int id) {
  char filename[128];
  sprintf(filename, "%s_%08x", name.c_str(), id);
  return std::string(filename);
}

void StateData::add( Array* array ) {
  allocated_arrays_.push_back(array);
  shared_ptr<MemoryUpdate> Update(new MemoryUpdate(array));
  updates_.add_new(Update);
}

shared_ptr<MemoryUpdate> StateData::find(const std::string name) const {
  return updates_.find(name);
}

// TODO Replace with templated function
void StateData::addCommonUpdates(const PathSegmentSet &pss) {
  std::set<shared_ptr<MemoryUpdate> > updates;

  bool init = true;
  foreach (ps, pss.begin(), pss.end()) {
    ConstraintSetFamily csf = (*ps)->constraints();
    if (init) {
      init = false;
      foreach (MU, csf.getImpliedUpdates().begin(), 
                   csf.getImpliedUpdates().end()) {
        updates.insert(MU->second);
      }
    } else {
      foreach (MU, csf.getImpliedUpdates().begin(), 
                   csf.getImpliedUpdates().end()) {
        if (!updates.count(MU->second)) {
          updates.erase(MU->second);
        }
      }
    }
  }
  DEBUG(llvm::cout << "COMMON UPDATES:\n");
  foreach (update, updates.begin(), updates.end()) {
    add(*update);
    DEBUG(llvm::cout << *(*update) << "\n");
  }
}

// TODO Replace with templated function
void StateData::addCommonUpdates(const PathSegletSet &pss) {
  std::set<shared_ptr<MemoryUpdate> > updates;

  bool init = true;
  foreach (ps, pss.begin(), pss.end()) {
    const ConstraintSetFamily *csf = &(*ps).constraints();
    if (init) {
      init = false;
      foreach (MU, csf->getImpliedUpdates().begin(), 
                   csf->getImpliedUpdates().end()) {
        updates.insert(MU->second);
      }
    } else {
      foreach (MU, csf->getImpliedUpdates().begin(), 
                   csf->getImpliedUpdates().end()) {
        if (!updates.count(MU->second)) {
          updates.erase(MU->second);
        }
      }
    }
  }
  DEBUG(llvm::cout << "COMMON UPDATES:\n");
  foreach (update, updates.begin(), updates.end()) {
    add(*update);
    DEBUG(llvm::cout << *(*update) << "\n");
  }
}

bool StateData::addFromFile( const std::string &filename ) {

  last_file_added_ = filename;
  std::vector< const Array* > constantArrays; 
  bool success;
  if (success = getConstantArraysFromFile(filename, constantArrays)) {
    foreach (it, constantArrays.begin(), constantArrays.end()) {
      const Array *A = *it;
      std::string name = A->name;

      let (replacement_name, g_array_names->find(name));
      if (replacement_name != g_array_names->end())
        name = replacement_name->second;

      int id = id_-1;
      if (name.find(suffix_, name.size() - suffix_.size()) != std::string::npos) {
        name = name.substr(0,name.size() - suffix_.size());
        id = id_;
      }
      std::string id_name = getNameWithID(name, id);
   
      add(new Array(id_name, A->size, 
                    &(*A->constantValues.begin()), 
                    &(*A->constantValues.end())));
      delete A;
    }
  }
  return success;
}

bool StateData::isRebuiltArray(const Array* array) {
  if (rebuilt_arrays_.count(array)) return true;
  return false;
}

#define XDEBUG(X)

const Array* StateData::rebuildArrayFromGeneric(const Array* array, 
                                                StateData* previous) {
  assert(!isRebuiltArray(array) && "rebuilding non generic Array");

  std::string name = array->name;
  //llvm::cout << "Searching for array: " << name << "\n";
  //llvm::cout << "Previous StateData: " << *previous << "\n";
  //llvm::cout << "Current StateData: " << *this << "\n";

  if (name.find(suffix_, name.size() - suffix_.size()) != std::string::npos) {
    std::string basename = name.substr(0,name.size() - suffix_.size());
    if (arrays_.find(basename) == arrays_.end()) {
      XDEBUG(llvm::cout << "building new array: " << name 
            << " --> " << getNameWithID(basename) << "\n");
      Array *array_new = new Array(getNameWithID(basename), array->size,
                                    &(*array->constantValues.begin()), 
                                    &(*array->constantValues.end()));
      arrays_[basename] = array_new;
      allocated_arrays_.push_back(array_new);
    }
    rebuilt_arrays_.insert(arrays_[basename]);
    marked_array_names_.insert(arrays_[basename]->name);
    return arrays_[basename];
  }

  if (previous->id() == 1) {
    // Bootstrap the State arrays_ of round 1
    if (previous->arrays_.find(name) == previous->arrays_.end()) {
      XDEBUG(llvm::cout << "(bootstrap) building new array: " << name << " --> "
        << previous->getNameWithID(name) << "\n");
      Array *array_new = new Array(previous->getNameWithID(name), 
                                          array->size,
                                          &(*array->constantValues.begin()), 
                                          &(*array->constantValues.end()));
      previous->arrays_[name] = array_new;
      allocated_arrays_.push_back(array_new);
    }
  }

  // Allow previously undefined arrays if they represent an event, such as input
  //assert(previous->arrays_.find(name) != previous->arrays_.end() 
  //       && "array name is undefined in previous state");
  if (previous->arrays_.find(name) == previous->arrays_.end()) {
    XDEBUG(llvm::cout << "array \"" << name << "\" is undefined in previous state\n")
		XDEBUG(llvm::cout << "(bootstrap) building new array: " << name << " --> "
			<< previous->getNameWithID(name) << "\n");
		Array *array_new = new Array(previous->getNameWithID(name), 
																				array->size,
																				&(*array->constantValues.begin()), 
																				&(*array->constantValues.end()));
		previous->arrays_[name] = array_new;
		allocated_arrays_.push_back(array_new);
  }
  
  rebuilt_arrays_.insert(previous->arrays_[name]);
  return previous->arrays_[name];
}

#undef XDEBUG

void StateData::print(std::ostream &os) const {
  os << "ID: " << id_ << " File: " << last_file_added_ << "\n";
  os << updates_;
}

//===----------------------------------------------------------------------===//
// Class GenericConstraint

//class ExprReplaceVisitor2 : public ExprVisitor {
//private:
//  const std::map< ref<Expr>, ref<Expr> > &replacements;
//
//public:
//  ExprReplaceVisitor2(const std::map< ref<Expr>, ref<Expr> > &_replacements) 
//    : ExprVisitor(true),
//      replacements(_replacements) {}
//
//  Action visitExprPost(const Expr &e) {
//    std::map< ref<Expr>, ref<Expr> >::const_iterator it =
//      replacements.find(ref<Expr>((Expr*) &e));
//    if (it!=replacements.end()) {
//      return Action::changeTo(it->second);
//    } else {
//      return Action::doChildren();
//    }
//  }
//};

GenericConstraint::GenericConstraint( std::string &filename ) 
  : filename_(filename), suffix_("__COPY__") {

  std::vector< ref<Expr> > constraints;
  if (getConstraintsFromFile(filename, constraints) == false) {
    exit(1);
  }

  constraints_ = constraints;
  constraint_ = exprFromVec(constraints.begin(), constraints.end(), Expr::And);

  //llvm::cout << "Before: \n";
  //ConstraintSetFamily before(constraint_);
  //foreach (CS, before.begin(), before.end())
  //  foreach(E, (*CS).begin(), (*CS).end())
  //    llvm::cout << *E << "\n";

  return;

  /*
  if (!RemoveUpdates) return;

  bool no_updates = false;
  ExprHashSet all_expr_set;
  while (!no_updates) {
    std::vector< ref<ReadExpr> > reads;
    findReads(constraint_, true, reads);

    std::map< ref<Expr>, ref<Expr> > replacements;
    ExprHashSet expr_set;
    foreach (read, reads.begin(), reads.end()) {
      const Array *array = (*read)->updates.root;
      for (const UpdateNode *un=(*read)->updates.head; un; un=un->next) {
        ref<Expr> RE 
          = ReadExpr::create(UpdateList(array, NULL), un->index);
        ref<Expr> E = EqExpr::create(un->value, RE);
        if (all_expr_set.insert(E).second)
          expr_set.insert(E);
      }
      if ((*read)->updates.head) {
        replacements[(*read)] 
          = ReadExpr::create(UpdateList(array, NULL), (*read)->index);
      }
    }

    if (replacements.size() > 0) {
      ref<Expr> replaced_constraint 
        = ExprReplaceVisitor2(replacements).visit(constraint_);
      constraint_ = replaced_constraint;
    }

    if (expr_set.size()) {
      ref<Expr> E = exprFromVec(expr_set.begin(), expr_set.end(), Expr::And);
      constraint_ = AndExpr::create(constraint_, E);
    }
    if (!expr_set.size() && !replacements.size()) no_updates = true;
  }

  //llvm::cout << "After:\n";
  //ConstraintSetFamily after(constraint_);
  //foreach (CS, after.begin(), after.end())
  //  foreach(E, (*CS).begin(), (*CS).end())
  //    llvm::cout << *E << "\n";

  std::vector< ref<Expr> > stack;
  stack.push_back(constraint_);
  while (stack.size() > 0) {
    ref<Expr> e = stack.back();
    stack.pop_back();

    switch (e->getKind()) {
      case Expr::And: {
        BinaryExpr *be = cast<BinaryExpr>(e);
        stack.push_back(be->left);
        stack.push_back(be->right);
        break;
      }
      default: {
        constraints_.push_back(e);
        break;
      }
    }
  }
  */
}

ref<Expr> GenericConstraint::generate(StateData *pre, 
                                      StateData *post) {

  ConstraintGenerator<StateData> generator(pre, post);
  RenameVisitor rename_visitor(pre, post);
  //ConcreteUpdateInserter<StateData> inserter(pre, post);

  //return generator.visit(rename_visitor.visit(exprFromVec(constraints_.begin(), 
  //                                                        constraints_.end(), 
  //                                                        Expr::And)));

  // Instead of generating the Expr in one shot, call visit once for each
  // constraint to allow an early break if a false constraint is built.

	//DEBUG(llvm::cout << "------\n");
  std::vector< ref<Expr> > generated;
  foreach (C, constraints_.begin(), constraints_.end()) {
    //ref<Expr> expr = inserter.visit(generator.visit(rename_visitor.visit(*C)));
    ref<Expr> expr = generator.visit(rename_visitor.visit(*C));
    generated.push_back(expr);

    if (ConstantExpr *CE = dyn_cast<ConstantExpr>(expr)) {
      if (CE->isFalse()) {
        //DEBUG(llvm::cout << "Constraint false when generated: " << rename_visitor.visit(*C) << "\n");
        break;
      }
    }
    //DEBUG(llvm::cout << "generated: " << expr << " from " << *C << "\n");
  }

  return exprFromVec(generated.begin(), generated.end(), Expr::And);
}

//===----------------------------------------------------------------------===//
// Class PathSeglet

PathSeglet::PathSeglet(StateData *pre, StateData *post, 
                       GenericConstraint *gc)
  : pre_state_(pre), post_state_(post), constraint_(gc->generate(pre,post)), 
  family_(constraint_), num_id_(++g_seglet_id) {}

PathSeglet::PathSeglet(ref<Expr> e) 
  : pre_state_(NULL), post_state_(NULL), constraint_(e), family_(constraint_),
  num_id_(++g_seglet_id) {}

bool PathSeglet::checkImpliedValues() {
  if (valid() && family_.checkForImpliedUpdates()) {
    return family_.applyImpliedUpdates();
  }
  return false;
}

void PathSeglet::print(std::ostream &os) const {
  os << "Constraints:\n";
  os << family_;
  os << "\nImpliedUpdates:\n";
  os << family_.getImpliedUpdates();
}

//===----------------------------------------------------------------------===//
// Class: PathSegment

PathSegment::PathSegment(const PathSegment &path_segment) {
  num_id_ = path_segment.num_id_;
  constraints_ = path_segment.constraints_;

  if (UseFileIds)
    foreach (id, path_segment.id_vec_.begin(), path_segment.id_vec_.end())
      id_vec_.push_back(*id);
}

PathSegment::PathSegment(const PathSeglet &seglet) {
  num_id_ = ++g_segment_id;
  add(seglet);
  if (constraints_.checkForImpliedUpdates())
    constraints_.applyImpliedUpdates();
}

PathSegment::PathSegment(const PathSegletArray &seglets) {
  num_id_ = ++g_segment_id;
  foreach (seglet, seglets.begin(), seglets.end()) 
    add(*seglet);
  if (constraints_.checkForImpliedUpdates())
    constraints_.applyImpliedUpdates();
}

bool PathSegment::equals(const PathSegment &ps) const {
  return constraints_.equals(ps.constraints_);
}

std::string PathSegment::id() {
  std::string result = "";
  if (UseFileIds) {
    foreach (id, id_vec_.begin(), id_vec_.end()) {
      if (result.size() == 0)
        result = *id;
      else
        result += ", " + *id;
    }
  } else {
    result = llvm::utohexstr(num_id_);
  }
  return result;
}

// Add a PathSeglet
void PathSegment::add(const PathSeglet &seglet) {
  if (id_vec_.size())
    constraints_.add(seglet.family());
  else
    constraints_ = ConstraintSetFamily(seglet.family());

  if (UseFileIds)
    id_vec_.push_back(seglet.id());
}

bool PathSegment::extend(const PathSegment &segment) {

  //constraints_.set_mark(false);
  //constraints_.add(segment.constraints());
	
	PathSegment tmp_seg(segment);
	tmp_seg.set_mark(false);
  constraints_.add(tmp_seg.constraints());

  if (UseFileIds)
    foreach (id, segment.id_vec_.begin(), segment.id_vec_.end())
      id_vec_.push_back(*id);

  return constraints_.valid();
}

class SymbolicObjectFinder : public ExprVisitor {
protected:
  Action visitRead(const ReadExpr &re) {
    const UpdateList &ul = re.updates;

    // XXX should we memo better than what ExprVisitor is doing for us?
    for (const UpdateNode *un=ul.head; un; un=un->next) {
      visit(un->index);
      visit(un->value);
    }

    if (ul.root->isSymbolicArray())
      if (results.insert(ul.root).second)
        objects.push_back(ul.root);

    return Action::doChildren();
  }

public:
  std::set<const Array*> results;
  std::vector<const Array*> &objects;
  
  SymbolicObjectFinder(std::vector<const Array*> &_objects)
    : objects(_objects) {}
};


// Query each of the independent ConstraintSets 
bool PathSegment::query(Solver* solver, StateData* state_data, std::ostream &os) {
  TimeStatIncrementer timestat(fstats.time_PathSegment_query);
  unsigned sub_query_count = 0;
  bool valid = true;
  //OStream os = PrintQueries ? llvm::cout : OStream(0);
  //if (!PrintQueries) os = OStream(0); 
     
  os << "QUERY\n";
 
  // getInitialValues
  MemoryUpdateSet* implied_updates
    = const_cast<MemoryUpdateSet*>(&constraints_.getImpliedUpdates());
  ConcreteUpdateInserter<MemoryUpdateSet> inserter(implied_updates);
  constraints_.applyImpliedUpdates();

  os << "IMPLIED UPDATES: \n" << *implied_updates << "\n";

  
  foreach (constraint, constraints_.begin(), constraints_.end()) {
    if (valid && (*constraint).size() > 0) {
      os << "\n\tSUB-QUERY: " << ++sub_query_count << "\n";

      std::vector<ref<Expr> > exprs;
      //ImpliedExprFinder<MemoryUpdateSet> finder(implied_updates);

      std::vector< const Array* > objects;
      SymbolicObjectFinder of(objects);

      os << "\t>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>\n";
      os << *constraint << "\n";
      foreach (E, (*constraint).begin(), (*constraint).end()) {
        //finder.visit(*E);
        exprs.push_back(inserter.visit(*E));
        //exprs.push_back(*E);
        of.visit(exprs.back());
        os << exprs.back() << "\n";
      }
      os << "\t<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<\n";

      /*
      foreach (E, finder.results.begin(), finder.results.end()) {
        exprs.push_back(*E);
        os << exprs.back() << "\n";
        of.visit(exprs.back());
      }
      */

      fstats.queries++;
      std::vector< std::vector<unsigned char> > result;
      WallTimer solvertime;
      if (solver->getInitialValues(Query(ConstraintManager(exprs),
                                   ConstantExpr::create(0, Expr::Bool)),
                                   objects, result)) {
        fstats.time_PathSegment_query_solvertime += solvertime.check();
        os << "\tPASS with Counterexamples:\n";
        for (unsigned i = 0, e = result.size(); i != e; ++i) {
          os << "\tArray " << i << ":\t"
                     << objects[i]->name
                     << "[";
          for (unsigned j = 0; j != objects[i]->size; ++j) {
            os << (unsigned) result[i][j];
            if (j + 1 != objects[i]->size)
              os << ", ";
          }
          os << "]\n";
        }
      } else {
        fstats.time_PathSegment_query_solvertime += solvertime.check();
        os << "\tFAIL\n";
        valid = false;
      }
    }
  }

  if (valid) {
    fstats.removed_constraints += 
			constraints_.removeNonMarkedConstraints(state_data->getMarkedArrayNames(), os);
    os << "Removed " << fstats.removed_constraints << " constraints.\n";
    os << "PASS: " << id() << "\n\n";
  } else {
    os << "FAIL: " << id() << "\n\n";
    fstats.fail++;
  }

  return valid;
}

//===----------------------------------------------------------------------===//
// Class: PathManager

PathManager::PathManager() : solver_(NULL) {
  states_.push_back(new StateData());
  
  g_array_names = new std::map<std::string,std::string>();

  (*g_array_names)[std::string("keyboard_vector")] = std::string("keyv");
  (*g_array_names)[std::string("g_klee_sent_keyv")] 
    = std::string("g_klee_sent_keyv__COPY__");
  (*g_array_names)[std::string("keyv")] 
    = std::string("keyv__COPY__");
  (*g_array_names)[std::string("last_loops")] 
    = std::string("XXXlast_loops__COPY__XXX");
  //  = std::string("last_loops__COPY__");
  (*g_array_names)[std::string("last_keyboard_change")] 
    = std::string("last_keyboard_change__COPY__");
}

void PathManager::reset_solver() {
  if (solver_) delete solver_;

  Solver *S = UseDummySolver ? createDummySolver() : new STPSolver(UseForkedSTP);

  if (UseSTPQueryPCLog) {
    std::string pclogfile = 
      "stp-queries/stp-queries-" +llvm::utohexstr(states_.back()->id()) + ".pc";
    llvm::cout << "Creating PCLoggingSolver: " << pclogfile << "\n";
    S = createPCLoggingSolver(S, pclogfile);
  }

  if (UseCachingSolver) {
    if (UseFastCexSolver)
      S = createFastCexSolver(S);
    S = createCexCachingSolver(S);
    //S = createCachingSolver(S);
    //S = createIndependentSolver(S);
  }
  solver_ = S;
}

void PathManager::init(std::string initial_state) {
  reset_solver();

  chains_ = new PathSegmentSet();
  StateData *sd = new StateData();
  states_.push_back(sd);

  if (InitialStateIsLog) {
    states_.back()->addFromFile(initial_state);
    PathSeglet seglet(ConstantExpr::create(1, Expr::Bool));
    chains_->push_back(new PathSegment(seglet));
  } else {
    PathSeglet seglet(previous(), current(), new GenericConstraint(initial_state));
    seglet.checkImpliedValues();
    seglet.set_id(initial_state);
    chains_->push_back(new PathSegment(seglet));
    //if (CommonUpdates) 
    current()->addCommonUpdates(*chains_);
  }
}

void PathManager::printCommonUpdates() {
  MemoryUpdateSet mus;
  std::set<shared_ptr<MemoryUpdate> > updates;

  bool init = true;
  foreach (chain, chains_->begin(), chains_->end()) {
    ConstraintSetFamily csf = (*chain)->constraints();
    if (init) {
      init = false;
      foreach (MU, csf.getImpliedUpdates().begin(), 
                   csf.getImpliedUpdates().end()) {
        updates.insert(MU->second);
      }
    } else {
      foreach (MU, csf.getImpliedUpdates().begin(), 
                   csf.getImpliedUpdates().end()) {
        if (!updates.count(MU->second)) {
          updates.erase(MU->second);
        }
      }
    }
  }

  DEBUG(llvm::cout << "COMMON UPDATES:\n");
  foreach (update, updates.begin(), updates.end()) {
    DEBUG(llvm::cout << *(*update) << "\n");
  }
}

PathSegmentSet* PathManager::makeSegments(const PathSegletSetChain &chain) {
  PathSegmentSet* segments = new PathSegmentSet();
  PathSegletArray path;

  makeSegmentsHelper(*segments, path, chain, 0);

  return segments;
}

void PathManager::makeSegmentsHelper(PathSegmentSet &segments, 
                               const PathSegletArray &path,
                               const PathSegletSetChain &chain,
                               unsigned index ) {
  //llvm::cout << "makeSegmentsHelper: " << index 
  //  << ", " << segments.size() 
  //  << ", " << path.size()
  //  << ", " << chain.size() << "\n";

  if (index == chain.size()) {
    PathSegment *segment = new PathSegment(path);
    if (segment->valid())
      segments.push_back(segment);
    else
      delete segment;
    return;
  }

  PathSegletArray extended_path = path;

  foreach (seglet, chain[index].begin(), chain[index].end()) {
    extended_path.push_back(*seglet);
    makeSegmentsHelper(segments, extended_path, chain, index+1);
    extended_path.pop_back();
  }
}

void PathManager::extendChains( const PathSegletSetChain &seglet_set_chain ) {

  TimeStatIncrementer timestat(fstats.time_PathManager_extendChains);
  PathSegmentSet *candidates = makeSegments(seglet_set_chain);
  fstats.segment_candidates = candidates->size();
  DEBUG(llvm::cout << "# of Candidate Segments: "<< candidates->size() << "\n");

  PathSegmentSet *extended_chains = new PathSegmentSet();

  int index = 0;
  foreach( segment, candidates->begin(), candidates->end()) {
    PathSegment basechain(*(*segment));
    basechain.checkForImpliedUpdates();
    basechain.set_mark(true);
    if (basechain.valid()) {
      foreach( chain, chains_->begin(), chains_->end()) {
        index++;
        PathSegment *extended_chain = new PathSegment(basechain);
        if (extended_chain->extend(*(*chain))) {
          bool duplicate_chain = false;
          foreach( ec, extended_chains->begin(), extended_chains->end()) {
            if ((*ec)->equals(*extended_chain)) {
              duplicate_chain = true;
              break;
            }
          }
          if (!duplicate_chain) {
            fstats.valid_chains++;
            extended_chains->push_back(extended_chain);
            DEBUG(llvm::cout << "Valid PathSegment Chain: " << index << " "
              << extended_chain->id() << "\n");
            DEBUG(llvm::cout << "<BEGIN SEGMENT CHAIN>\n");
            DEBUG(llvm::cout << extended_chain->constraints() << "\n");
            DEBUG(llvm::cout <<  "\n<END SEGMENT CHAIN>\n");
          } else {
            fstats.duplicate_chains++;
            DEBUG(llvm::cout << "Duplicate PathSegment Chain: " << index << " "
              << extended_chain->id() << "\n");
            delete extended_chain;
          }
        } else {
          fstats.invalid_chains++;
          DEBUG(llvm::cout << "Invalid Path-Segment Chain: " << index << " "
            << extended_chain->id() << "\n");
          delete extended_chain;
        }
      }
    }
  }

  foreach( chain, chains_->begin(), chains_->end()) delete *chain;
  delete chains_;
  delete candidates;

  DEBUG(llvm::cout << "# of Candidate Chains: " 
    << extended_chains->size() << "\n");

  chains_ = extended_chains;
}

int PathManager::queryChains() {
  PathSegmentSet *valid_chains = new PathSegmentSet();
  foreach (chain, chains_->begin(), chains_->end()) {
		bool valid_chain = false;
		std::stringstream ss;
		if (PrintQueries) llvm::cout << "Query on chain " << (*chain)->id() << "\n";
    if ((*chain)->valid() && (*chain)->query(solver_, current(), ss)) {
			// Check for duplicates again b/c query() removed non-marked constarints
			bool duplicate = false;
			foreach (vc, valid_chains->begin(), valid_chains->end()) {
				if ((*vc)->equals(**chain)) {
					duplicate = true;
					break;
				}
			}
			if (!duplicate) valid_chain = true;
    }
		if (valid_chain) {
			if (PrintQueries) llvm::cout << "Chain passed query.\n" << ss.str();
			valid_chains->push_back(*chain);
		} else {
			if (PrintQueries) llvm::cout << "Chain failed query.\n";
			delete *chain;
		}
  }

  reset_solver();

  delete chains_;
  chains_ = valid_chains;

  if (PrintChains) {
    int count = 0;
    foreach (chain, chains_->begin(), chains_->end()) {
      llvm::cout << "CHAIN (" << ++count << ") " << (*chain)->id() << "\n";
      llvm::cout << "----------------------------\n";
      llvm::cout << (*chain)->constraints()  << "\n";
      llvm::cout << "----------------------------\n";
    }
  }
  
  //if (uint64_t queries = *theStatisticManager->getStatisticByName("Queries")) {
  //  llvm::cout 
  //    << "--\n"
  //    << "total queries = " << queries << "\n"
  //    << "total queries constructs = " 
  //    << *theStatisticManager->getStatisticByName("QueriesConstructs") << "\n"
  //    << "valid queries = " 
  //    << *theStatisticManager->getStatisticByName("QueriesValid") << "\n"
  //    << "invalid queries = " 
  //    << *theStatisticManager->getStatisticByName("QueriesInvalid") << "\n"
  //    << "query cex = " 
  //    << *theStatisticManager->getStatisticByName("QueriesCEX") << "\n";
  //}

  if (CommonUpdates)
    current()->addCommonUpdates(*chains_);

  return chains_->size();
}

void PathManager::addStateData(std::string filename) {
  StateData* sd = new StateData();
  sd->addFromFile(filename);
  states_.push_back(sd);

  DEBUG(llvm::cout << "StateData: \n" << *(states_.back()) << "\n");

  if (StateDataCount > 0) {
    while (states_.size() > StateDataCount) {
      delete states_.front();
      states_.pop_front();
    }
  }
}

StateData* PathManager::previous() {
  if (states_.size() > 1) 
    return states_[states_.size()-2];
  return NULL;
}

StateData* PathManager::current() {
  if (states_.size() > 0) 
    return states_[states_.size()-1];
  return NULL;
}

//===----------------------------------------------------------------------===//

