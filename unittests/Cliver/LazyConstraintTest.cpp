//===-- LazyConstraintTest.cpp ----------------------------------*- C++ -*-===//
//
// Unit tests for the LazyConstraint and LazyConstraintDispatcher classes.
//
//===----------------------------------------------------------------------===//

#include <iostream>
#include "gtest/gtest.h"

#include "llvm/Support/raw_ostream.h"
#include "klee/Expr.h"
#include "klee/Constraints.h"
#include "klee/Solver.h"
#include "cliver/LazyConstraint.h"

using namespace cliver;
using namespace klee;


class LazyConstraintTest : public ::testing::Test {
public:

  virtual void SetUp() {
    x_array = Array::CreateArray("x", sizeof(unsigned int));
    y_array = Array::CreateArray("y", sizeof(unsigned int));

    read_x = Expr::createTempRead(x_array, Expr::Int32);
    ref<Expr> one = ConstantExpr::alloc(1U, Expr::Int32);
    ref<Expr> xplusone = AddExpr::create(one, read_x);
    xplusone_squared = MulExpr::create(xplusone, xplusone);

    read_y = Expr::createTempRead(y_array, Expr::Int32);
    ref<Expr> xplusy = AddExpr::create(read_x, read_y);
    xplusy_squared = MulExpr::create(xplusy, xplusy);

    for (unsigned int i = 0; i < 4; i++) {
      ref<Expr> x_byte = ExtractExpr::create(read_x, 8*i, Expr::Int8);
      x_exprs.push_back(x_byte);
      ref<Expr> y_byte = ExtractExpr::create(read_y, 8*i, Expr::Int8);
      y_exprs.push_back(y_byte);
    }

    // construct standard solver chain
    STPSolver *stpSolver = new STPSolver(true);
    solver = stpSolver;
    solver = createCexCachingSolver(solver);
    solver = createCachingSolver(solver);
    solver = createIndependentSolver(solver);
  }

  virtual void TearDown() {
    // Array destructor is disabled; cannot delete x_array
    delete solver;
  }

  const Array *x_array, *y_array;
  LazyConstraint::ExprVec x_exprs;
  LazyConstraint::ExprVec y_exprs;
  ref<Expr> read_x;
  ref<Expr> read_y;
  ref<Expr> xplusone_squared;
  ref<Expr> xplusy_squared;
  Solver *solver;
};


namespace {

// sample "prohibitive" function
unsigned int p(unsigned int x) {
  return 641 * x;
}

// inverse of sample "prohibitive" function
unsigned int p_inv(unsigned int x) {
  return 6700417 * x;
}

// trigger version of p()
int trigger_p(const unsigned char *in_buf, size_t in_len,
              unsigned char *out_buf, size_t out_len) {
  const unsigned int *input = (const unsigned int *)in_buf;
  unsigned int *output = (unsigned int *)out_buf;
  if (input == NULL || in_len != sizeof(*input))
    return -1;
  if (output == NULL || out_len != sizeof(*output))
    return -1;

  *output = p(*input);

  return 0; // success
}

// trigger version of p_inv()
int trigger_p_inv(const unsigned char *in_buf, size_t in_len,
                  unsigned char *out_buf, size_t out_len) {
  const unsigned int *input = (const unsigned int *)in_buf;
  unsigned int *output = (unsigned int *)out_buf;
  if (input == NULL || in_len != sizeof(*input))
    return -1;
  if (output == NULL || out_len != sizeof(*output))
    return -1;

  *output = p_inv(*input);

  return 0; // success
}

TEST(ProhibitiveFunction, Pof10) {
  EXPECT_EQ(p(10), (unsigned int)6410);
  EXPECT_EQ(p_inv(6410), (unsigned int)10);
}

TEST(ProhibitiveFunction, PThenPinv) {
  unsigned int n = 314159;
  EXPECT_EQ(p_inv(p(n)), n);
}

TEST(ProhibitiveFunction, PinvThenP) {
  unsigned int n = 314159;
  EXPECT_EQ(p(p_inv(n)), n);
}

TEST_F(LazyConstraintTest, ExprToString) {
  EXPECT_EQ(exprToString(xplusone_squared),
            "N0:(Mul w32 N1:(Add w32 1 N2:(ReadLSB w32 0 x)) N1)");
  EXPECT_EQ(
      exprToString(xplusy_squared),
      "N0:(Mul w32 N1:(Add w32 N2:(ReadLSB w32 0 x) N3:(ReadLSB w32 0 y)) N1)");
}

TEST_F(LazyConstraintTest, SubstitutionOneVariable) {
  ref<Expr> two = ConstantExpr::alloc(2U, Expr::Int32);
  ref<Expr> x_equals_2 = EqExpr::create(read_x, two);

  ConstraintManager cm;
  cm.addConstraint(x_equals_2);
  ref<Expr> nine = cm.simplifyExpr(xplusone_squared);
  EXPECT_EQ(exprToString(nine), "9");
}

TEST_F(LazyConstraintTest, SubstitutionBytewise) {
  UpdateList x_ul(x_array, 0);
  ref<Expr> read_x_0 =
      ReadExpr::create(x_ul, ConstantExpr::create(0, Expr::Int32));
  ref<Expr> read_x_1 =
      ReadExpr::create(x_ul, ConstantExpr::create(1, Expr::Int32));
  ref<Expr> read_x_2 = ExtractExpr::create(read_x, 16, Expr::Int8);
  ref<Expr> read_x_3 = ExtractExpr::create(read_x, 24, Expr::Int8);
  ref<Expr> zero_byte = ConstantExpr::alloc(0U, Expr::Int8);
  ref<Expr> two_byte = ConstantExpr::alloc(2U, Expr::Int8);

  ref<Expr> x0_equals_2 = EqExpr::create(read_x_0, two_byte);
  ref<Expr> x1_equals_0 = EqExpr::create(read_x_1, zero_byte);
  ref<Expr> x2_equals_0 = EqExpr::create(read_x_2, zero_byte);
  ref<Expr> x3_equals_0 = EqExpr::create(read_x_3, zero_byte);

  ConstraintManager cm;
  cm.addConstraint(x0_equals_2);
  cm.addConstraint(x1_equals_0);
  cm.addConstraint(x2_equals_0);
  cm.addConstraint(x3_equals_0);

  ref<Expr> nine = cm.simplifyExpr(xplusone_squared);
  EXPECT_EQ(exprToString(nine), "9");
}

TEST_F(LazyConstraintTest, SubstitutionTwoVariable) {
  ref<Expr> two = ConstantExpr::alloc(2U, Expr::Int32);
  ref<Expr> x_equals_2 = EqExpr::create(read_x, two);

  ref<Expr> three = ConstantExpr::alloc(3U, Expr::Int32);
  ref<Expr> y_equals_3 = EqExpr::create(read_y, three);

  ConstraintManager cm;
  cm.addConstraint(x_equals_2);
  cm.addConstraint(y_equals_3);
  ref<Expr> twentyfive = cm.simplifyExpr(xplusy_squared);
  EXPECT_EQ(exprToString(twentyfive), "25");
  EXPECT_EQ(twentyfive->getKind(), Expr::Constant);
  EXPECT_TRUE(isa<ConstantExpr>(twentyfive)); // equivalent to previous line
}

TEST_F(LazyConstraintTest, SubstitutionViaEvaluate) {
  std::vector<const Array*> objects;
  std::vector< std::vector<unsigned char> > values;

  objects.push_back(x_array);
  values.push_back(std::vector<unsigned char>{2,0,0,0});

  Assignment as_incomplete(objects, values, true); // allowFreeValues = true
  ref<Expr> yplus2_squared = as_incomplete.evaluate(xplusy_squared);
  EXPECT_EQ(exprToString(yplus2_squared),
            "N0:(Mul w32 N1:(Add w32 2 N2:(ReadLSB w32 0 y)) N1)");

  objects.push_back(y_array);
  values.push_back(std::vector<unsigned char>{3,0,0,0});

  Assignment as(objects, values, true); // allowFreeValues = true
  ref<Expr> twentyfive = as.evaluate(xplusy_squared);
  EXPECT_EQ(exprToString(twentyfive), "25");
  EXPECT_EQ(twentyfive->getKind(), Expr::Constant);
  EXPECT_TRUE(isa<ConstantExpr>(twentyfive)); // equivalent to previous line
}

TEST_F(LazyConstraintTest, SolveForUniqueY) {
  // if x == 10 and y_i == x_i (bytewise), then what is y?
  ref<Expr> ten = ConstantExpr::alloc(10U, Expr::Int32);
  ref<Expr> x_equals_10 = EqExpr::create(read_x, ten);

  ref<Expr> y_equals_x = ConstantExpr::alloc(1U, Expr::Bool);
  for (size_t i = 0; i < 4; i++) {
    ref<Expr> x_i = ExtractExpr::create(read_x, i*8, Expr::Int8);
    ref<Expr> y_i = ExtractExpr::create(read_y, i*8, Expr::Int8);
    ref<Expr> byte_constraint = EqExpr::create(x_i, y_i);
    y_equals_x = AndExpr::create(y_equals_x, byte_constraint);
  }

  ConstraintManager cm;
  cm.addConstraint(x_equals_10);
  // KLEE's expression simplification heuristics fail to discover y=10
  // (byte-wise), so we have to get a SAT solver involved. Uncomment the
  // following line to see for yourself.
  // std::cout << exprToString(cm.simplifyExpr(y_equals_x)) << "\n";
  cm.addConstraint(y_equals_x);

  Query query(cm, ConstantExpr::alloc(0, Expr::Bool));
  std::vector<const Array *> arrays;
  arrays.push_back(y_array);
  std::vector< std::vector<unsigned char> > initial_values;

  // Solve for a satisfying assignment.
  bool result = solver->getInitialValues(query, arrays, initial_values);
  EXPECT_TRUE(result);
  EXPECT_EQ(initial_values.size(), 1U);
  EXPECT_EQ(initial_values[0][0], 10U);
  EXPECT_EQ(initial_values[0][1], 0);
  EXPECT_EQ(initial_values[0][2], 0);
  EXPECT_EQ(initial_values[0][3], 0);

  // Prove that it is the *unique* solution.
  Assignment as(arrays, initial_values, true);
  ref<Expr> as_expr = assignmentToExpr(as);
  solver->mustBeTrue(Query(cm, as_expr), result);
  EXPECT_TRUE(result);
}

TEST_F(LazyConstraintTest, SolveForUniqueY2) {
  // if x == 10 and y_i == x_i (bytewise), then what is y?
  ref<Expr> ten = ConstantExpr::alloc(10U, Expr::Int32);
  ref<Expr> x_equals_10 = EqExpr::create(read_x, ten);

  LazyConstraint::ExprVec exprs;
  for (size_t i = 0; i < sizeof(unsigned int); i++) {
    ref<Expr> byte = ExtractExpr::create(read_x, i*8, Expr::Int8);
    exprs.push_back(byte);
  }

  ConstraintManager cm;
  cm.addConstraint(x_equals_10);

  std::vector<unsigned char> unique_values;
  bool result = solveForUniqueExprVec(solver, cm, exprs, unique_values);
  EXPECT_TRUE(result);
  EXPECT_EQ(unique_values.size(), sizeof(unsigned int));
  EXPECT_EQ(unique_values[0], 10U);
  EXPECT_EQ(unique_values[1], 0);
  EXPECT_EQ(unique_values[2], 0);
  EXPECT_EQ(unique_values[3], 0);
}

TEST_F(LazyConstraintTest, SolveForNonUniqueY) {
  // if x < 10 and y_i == x_i (bytewise), then what is y?
  ref<Expr> ten = ConstantExpr::alloc(10U, Expr::Int32);
  ref<Expr> x_ult_10 = UltExpr::create(read_x, ten);

  ref<Expr> y_equals_x = ConstantExpr::alloc(1U, Expr::Bool);
  for (size_t i = 0; i < 4; i++) {
    ref<Expr> x_i = ExtractExpr::create(read_x, i*8, Expr::Int8);
    ref<Expr> y_i = ExtractExpr::create(read_y, i*8, Expr::Int8);
    ref<Expr> byte_constraint = EqExpr::create(x_i, y_i);
    y_equals_x = AndExpr::create(y_equals_x, byte_constraint);
  }

  ConstraintManager cm;
  cm.addConstraint(x_ult_10);
  // std::cout << exprToString(cm.simplifyExpr(y_equals_x)) << "\n";
  cm.addConstraint(y_equals_x);

  Query query(cm, ConstantExpr::alloc(0, Expr::Bool));
  std::vector<const Array *> arrays;
  arrays.push_back(y_array);
  std::vector< std::vector<unsigned char> > initial_values;

  // Solve for a satisfying assignment.
  bool result = solver->getInitialValues(query, arrays, initial_values);
  EXPECT_TRUE(result);
  EXPECT_EQ(initial_values.size(), 1U);
  EXPECT_LT(initial_values[0][0], 10U);
  EXPECT_EQ(initial_values[0][1], 0);
  EXPECT_EQ(initial_values[0][2], 0);
  EXPECT_EQ(initial_values[0][3], 0);

  // Try to prove that it is the *unique* solution (and fail)
  Assignment as(arrays, initial_values, true);
  ref<Expr> as_expr = assignmentToExpr(as);
  solver->mustBeTrue(Query(cm, as_expr), result);
  EXPECT_FALSE(result);
}

TEST_F(LazyConstraintTest, SolveForNonUniqueY2) {
  // if x < 10 and y_i == x_i (bytewise), then what is y?
  ref<Expr> ten = ConstantExpr::alloc(10U, Expr::Int32);
  ref<Expr> x_ult_10 = UltExpr::create(read_x, ten);

  LazyConstraint::ExprVec exprs;
  for (size_t i = 0; i < sizeof(unsigned int); i++) {
    ref<Expr> byte = ExtractExpr::create(read_x, i*8, Expr::Int8);
    exprs.push_back(byte);
  }

  ConstraintManager cm;
  cm.addConstraint(x_ult_10);

  std::vector<unsigned char> unique_values;
  bool result = solveForUniqueExprVec(solver, cm, exprs, unique_values);
  EXPECT_FALSE(result);
}

TEST_F(LazyConstraintTest, SolveForNoSolutionY) {
  // if x < 10 and x > 9 and y_i == x_i (bytewise), then what is y?
  ref<Expr> nine = ConstantExpr::alloc(9U, Expr::Int32);
  ref<Expr> ten = ConstantExpr::alloc(10U, Expr::Int32);
  ref<Expr> x_ult_10 = UltExpr::create(read_x, ten);
  ref<Expr> x_ugt_9 = UgtExpr::create(read_x, nine);

  LazyConstraint::ExprVec exprs;
  for (size_t i = 0; i < sizeof(unsigned int); i++) {
    ref<Expr> byte = ExtractExpr::create(read_x, i*8, Expr::Int8);
    exprs.push_back(byte);
  }

  ConstraintManager cm;
  cm.addConstraint(x_ult_10);
  cm.addConstraint(x_ugt_9);

  std::vector<unsigned char> unique_values;
  bool result = solveForUniqueExprVec(solver, cm, exprs, unique_values);
  EXPECT_FALSE(result);
}

TEST_F(LazyConstraintTest, TriggerViaConstraintManager) {
  LazyConstraint lazy_p(x_exprs, y_exprs, trigger_p, "trigger_p");
  LazyConstraint lazy_p_inv(y_exprs, x_exprs, trigger_p_inv, "trigger_p_inv");

  ref<Expr> ten = ConstantExpr::alloc(10U, Expr::Int32);
  ref<Expr> x_equals_10 = EqExpr::create(read_x, ten);

  ConstraintManager cm;
  cm.addConstraint(x_equals_10);

  std::vector<ref<Expr>> new_constraints;
  bool p_success = lazy_p.trigger(solver, cm, new_constraints);
  EXPECT_TRUE(p_success);
  for (const auto &c : new_constraints) {
    // std::cout << exprToString(c) << "\n";
    cm.addConstraint(c);
  }

  ref<Expr> result_y = cm.simplifyExpr(read_y);
  EXPECT_EQ(exprToString(result_y), "6410");

  // We should now have enough constraints to trigger the inverse as well.
  bool p_inv_success = lazy_p_inv.trigger(solver, cm, new_constraints);
  EXPECT_TRUE(p_inv_success);
  for (const auto &c : new_constraints) {
    // std::cout << exprToString(c) << "\n";

    // These should already subsumed by the existing constraints, so adding
    // them back in shouldn't cause the program to abort.
    cm.addConstraint(c);
  }
}

TEST_F(LazyConstraintTest, TriggerViaAssignment) {
  LazyConstraint lazy_p(x_exprs, y_exprs, trigger_p, "trigger_p");

  std::vector<const Array *> arrays;
  arrays.push_back(x_array);
  std::vector< std::vector<unsigned char> > values;
  values.push_back(std::vector<unsigned char>{10, 0, 0, 0});
  Assignment as(arrays, values, true);

  std::vector<ref<Expr>> new_constraints;
  bool p_success = lazy_p.trigger(solver, as, new_constraints);
  EXPECT_TRUE(p_success);

  ConstraintManager cm;
  for (const auto &c : new_constraints) {
    // std::cout << exprToString(c) << "\n";
    cm.addConstraint(c);
  }
  ref<Expr> result_y = cm.simplifyExpr(read_y);
  EXPECT_EQ(exprToString(result_y), "6410");
}


} // end anonymous namespace
