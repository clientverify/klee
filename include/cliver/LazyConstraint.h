//===-- LazyConstraint.h ---------------------------------------*- C++ -*-===//
//
// <insert license>
//
//===---------------------------------------------------------------------===//
//
// LazyConstraint and LazyConstraintDispatcher classes
//
//===---------------------------------------------------------------------===//
//
// Text from the NSDI paper submission:
//
// There are several potentially useful extensions to our
// client verification algorithm that we are considering for future
// development.  Here we highlight one, namely \textit{lazy constraint
// generators} to accompany the designation of prohibitive functions.
// Since a function, once specified as prohibitive, will be skipped by
// the verifier until its inputs are inferred concretely, the verifier
// cannot gather constraints relating the input and output buffers of
// that function until the inputs can be inferred via other constraints.
// There are cases, however, where introducing constraints relating the
// input and output buffers once some subset of them are inferred
// concretely would be useful or, indeed, is central to eventually
// inferring other inputs concretely.
//
// Perhaps the most straightforward example arises in symmetric
// encryption modes that require the inversion of a block cipher in order
// to decrypt a ciphertext (e.g., CBC mode).  Upon reaching the client
// \sendInstr instruction for a message, the verifier reconciles the
// observed client-to-server message \msg{\msgNmbr} with the constraints
// \newState.\constraints accumulated on the path to that \sendInstr;
// for example, suppose this makes concrete the buffers corresponding to
// outputs of the encryption routine.  However, because the block cipher
// was prohibitive and so skipped, constraints relating the input buffers
// to those output buffers were not recorded, and so the input buffers
// remain unconstrained by the (now concrete) output buffers.  Moreover,
// a second pass of the client execution will not add additional
// constraints on those input buffers, meaning they will remain
// unconstrained after another pass.
//
// An extension to address this situation is to permit the user to
// specify a lazy constraint generator along with designating the block
// cipher as prohibitive.  The lazy constraint generator would simply be
// a function from some subset of the input and output buffers for the
// function to constraints on other buffers.  The generator is ``lazy''
// in that it would be invoked by the verifier only after its inputs were
// inferred concretely by other means; once invoked, it would produce new
// constraints as a function of those values.  In the case of the block
// cipher, the most natural constraint generator would be the inverse
// function, which takes in the key and a ciphertext and produces the
// corresponding plaintext to constrain the value of the input buffer.
//
// Our \openssl case study in \secref{sec:multipass:tls} does not require
// this functionality since in the encryption mode used there, the
// ciphertext and plaintext buffers are related by simple exclusive-or
// against outputs from the (still prohibitive) block cipher applied to
// values that can be inferred concretely from the message.  So, once the
// inputs to the block cipher are inferred by the verifier, the block
// cipher outputs can be produced concretely, and the plaintext then
// inferred from the concrete ciphertexts by exclusive-or.
//===---------------------------------------------------------------------===//
//
// Lazy Constraint overview
//
// The LazyConstraint class represents a lazy constraint L that is waiting for
// a particular set of expressions InE to be concretized, at which time it can
// be realized into a true constraint L(InE), and added to the path condition.
// Note that the set of expressions InE represents the "input" to L but may
// correspond to the output of a prohibitive function P, e.g., when the lazy
// constraint L represents the inverse of P.  A lazy constraint is a tuple L =
// (InE, OutE, f) as follows:
//
//   1. InE: A vector of input expressions containing symbolic variables.
//   2. OutE: A vector of output expressions containing symbolic variables.
//   3. f(): A function s.t. if InE and OutE were concrete, f(InE) = OutE.
//
// If at some point in time, the InE expression takes on a concrete value InC,
// L is "triggered" and realized into a true constraint, namely the expression
// OutE == f(InC).
//
// In order to create a lazy constraint L, call the following special function
// from the *bitcode* (or inside the DEFINE_MODEL of a prohibitive function):
//
//   cliver_lazy_constraint(uint8 *in_buf, size_t in_len,
//                          uint8 *out_buf, size_t out_len,
//                          const char *function_name,
//                          const char *taint) // taint string optional
//
// You must also designate a function in KLEE (not the bitcode) corresponding
// to "function_name" with the following signature:
//
//   int function_name(const uint8 *in_buf, size_t in_len,
//                     uint8 *out_buf, size_t out_len)
//
// This function pointer will be assigned to lazy constraint L, and will be
// executed when InE is available as a concrete value. Note that since there is
// only one input buffer and one output buffer, some serialization may be
// required in order to use this interface.
//
// Lazy Constraint Dispatcher overview
//
// The LazyConstraintDispatcher (LCD) manages the pile of lazy constraints that
// have accumulated.  One option that was considered was to extend the
// ConstraintManager, but from a testing and maintenance perspective, it is
// more manageable to keep this code separate from vanilla KLEE.  Logically,
// each LazyConstraintDispatcher is paired with and interacts with one
// ConstraintManager (hence, one per state).  Whenever the constraint manager
// adds a new constraint to the path condition, the LCD is invoked to determine
// whether any lazy constraints now have InE's that take on concrete values,
// i.e., triggering them.  If so, they are realized into true constraints and
// added to the path condition.  This may trigger other lazy constraints, and
// this process is continued until a fixed point is reached.
//
//===---------------------------------------------------------------------===//
#ifndef LIB_CLIVER_LAZYCONSTRAINT_H_
#define LIB_CLIVER_LAZYCONSTRAINT_H_

#include <vector>
#include <list>
#include <map>
#include <string>
#include <memory>

// #include "klee/ExecutionState.h"
#include "klee/util/ExprVisitor.h"
#include "klee/Solver.h"
#include "cliver/CVAssignment.h"

// namespace klee {
//   struct KFunction;
// }

namespace cliver {

class LazyConstraint
{
public:

  // Vector of expressions, each usually representing one byte of a
  // buffer. These expressions can be either symbolic formulas (representing
  // the data) or equality constraints (representing the triggered lazy
  // constraints), but each element always corresponds to one byte of a buffer.
  typedef std::vector< klee::ref<klee::Expr> > ExprVec;

  // A function that when triggered, populates out_buf and returns 0 on
  // success. The buffers in_buf and out_buf must be non-overlapping.
  typedef int (*TriggerFunc)(const unsigned char *in_buf, size_t in_len,
                             unsigned char *out_buf, size_t out_len);

  LazyConstraint(const ExprVec &in, const ExprVec &out, TriggerFunc f,
                 std::string fname, std::string taint = "")
      : in_exprs(in), out_exprs(out), trigger_func(f), trigger_func_name(fname),
        taint(taint) {}

  /// \brief Trigger (or realize) the lazy constraint.
  /// \param[in] solver SMT solver stack to be used for concretization.
  /// \param[in] cm ConstraintManager (maybe empty) covering in_expr variables.
  /// \param[in] as Assignment (maybe empty) covering in_expr variables.
  /// \pre NOTE: We assume "as" is consistent with the constraints in "cm".
  /// \param[out] new_constraints A vector of realized constraints.
  /// \return true on success; false if, e.g., we cannot concretize in_exprs.
  bool trigger(klee::Solver *solver, const klee::ConstraintManager &cm,
               const klee::Assignment &as,
               std::vector<klee::ref<klee::Expr>> &new_constraints) const;

  /// \brief Trigger the lazy constraint (with just a constraint manager)
  bool trigger(klee::Solver *solver, const klee::ConstraintManager &cm,
               std::vector<klee::ref<klee::Expr>> &new_constraints) const;

  /// \brief Trigger the lazy constraint (with just an assignment)
  bool trigger(klee::Solver *solver, const klee::Assignment &as,
               std::vector<klee::ref<klee::Expr>> &new_constraints) const;

  /// \brief Return the (nick)name of the trigger function.
  std::string name() const { return trigger_func_name; }

private:

  // Input and output (symbolic) expressions.  Each should be a vector of
  // expressions, with each element representing one byte of in_buf/out_buf.
  ExprVec in_exprs;
  ExprVec out_exprs;

  // Function to invoke concretely/natively when triggering the LazyConstraint
  TriggerFunc trigger_func;
  std::string trigger_func_name; // used by bitcode to identify the function

  // Taint information used when OPENSSL_SYMBOLIC_TAINT == 1
  std::string taint;

};


class LazyConstraintDispatcher
{
public:

  /// \brief Add lazy constraint to the dispatcher's cache.
  void addLazy(std::shared_ptr<LazyConstraint> lazy_c) {
    lazy_constraint_cache.push_back(lazy_c);
  }

  /// \brief number of lazy constraints cached
  size_t size() const { return lazy_constraint_cache.size(); }

  /// \brief Attempt to trigger all lazy constraints
  ///
  /// Once a lazy constraint has been triggered, remove it from the dispatcher's
  /// cache. If a lazy constraint triggers and causes a contradiction with the
  /// earlier constraints, the last LazyConstraint triggered (in the output)
  /// will be the contradicting constraint.
  ///
  /// \param[out] new_constraints - one Expr per triggered LazyConstraint.
  /// \param[out] triggered - List of LazyConstraints successfully triggered.
  /// \param[in] cm - Constraint manager with all initial constraints to apply.
  /// \param[in] recursive - Enable lazily-generated constraints to cascade and
  ///            trigger other lazy constraints recursively (default: true).
  /// \return True if the triggered lazy constraints (if any) are consistent
  ///            with all the initial constraints in the constraint manager.
  ///            Vacuously true if no LazyConstraints have been triggered.
  bool triggerAll(klee::Solver *solver,
                  std::vector<klee::ref<klee::Expr>> &new_constraints,
                  std::vector<std::shared_ptr<LazyConstraint>> &triggered,
                  const klee::ConstraintManager &cm, bool recursive = true);

private:

  std::list<std::shared_ptr<LazyConstraint>> lazy_constraint_cache;
};

/////////////////// Helper Functions //////////////////

std::string exprToString(klee::ref<klee::Expr> e);

void addAssignmentToConstraints(const klee::Assignment &as,
                                klee::ConstraintManager &cm);

klee::ref<klee::Expr> assignmentToExpr(const klee::Assignment &as);

klee::ref<klee::Expr>
conjunctAllExpr(const std::vector<klee::ref<klee::Expr>> &vex);

// If the constraints in cm imply a unique, concrete set of values for exprs,
// return true and assign those values to unique_values (output
// parameter). Otherwise, return false.
bool solveForUniqueExprVec(klee::Solver *solver,
                           const klee::ConstraintManager &cm,
                           const LazyConstraint::ExprVec &exprs,
                           std::vector<unsigned char> &unique_values);

}  // End cliver namespace

#endif  // LIB_CLIVER_LAZYCONSTRAINT_H_
