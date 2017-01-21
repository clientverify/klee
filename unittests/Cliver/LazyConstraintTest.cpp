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
#include "klee/util/ExprPPrinter.h"
#include "cliver/LazyConstraint.h"

using namespace cliver;
using namespace klee;


class LazyConstraintTest : public ::testing::Test {
public:

  virtual void SetUp() {
    x_array = Array::CreateArray("x", 32);
    y_array = Array::CreateArray("y", 32);

    ref<Expr> read_x = Expr::createTempRead(x_array, 32);
    ref<Expr> one = ConstantExpr::alloc(1U, Expr::Int32);
    ref<Expr> xplusone = AddExpr::create(one, read_x);
    xplusone_squared = MulExpr::create(xplusone, xplusone);

    ref<Expr> read_y = Expr::createTempRead(y_array, 32);
    ref<Expr> xplusy = AddExpr::create(read_x, read_y);
    xplusy_squared = MulExpr::create(xplusy, xplusy);
  }

  virtual void TearDown() {
    // Array destructor is disabled; cannot delete x_array
  }

  const Array *x_array, *y_array;
  ref<Expr> xplusone_squared;
  ref<Expr> xplusy_squared;
};


namespace {

unsigned int p(unsigned int x) {
  return 641 * x;
}

unsigned int p_inv(unsigned int x) {
  return 6700417 * x;
}

std::string exprToString(ref<Expr> e) {
  std::string expr_string;
  llvm::raw_string_ostream out(expr_string);
  ExprPPrinter *epp = ExprPPrinter::create(out);
  epp->setForceNoLineBreaks(true);
  epp->print(e);
  return out.str();
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
  ref<Expr> read_x = Expr::createTempRead(x_array, 32);
  ref<Expr> two = ConstantExpr::alloc(2U, Expr::Int32);
  ref<Expr> x_equals_2 = EqExpr::create(read_x, two);

  ConstraintManager cm;
  cm.addConstraint(x_equals_2);
  ref<Expr> nine = cm.simplifyExpr(xplusone_squared);
  EXPECT_EQ(exprToString(nine), "9");
}

TEST_F(LazyConstraintTest, SubstitutionBytewise) {
  UpdateList x_ul(x_array, 0);
  ref<Expr> read_x = Expr::createTempRead(x_array, 32);
  ref<Expr> read_x_0 = ReadExpr::create(x_ul, ConstantExpr::create(0, 32));
  ref<Expr> read_x_1 = ReadExpr::create(x_ul, ConstantExpr::create(1, 32));
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
  ref<Expr> read_x = Expr::createTempRead(x_array, 32);
  ref<Expr> two = ConstantExpr::alloc(2U, Expr::Int32);
  ref<Expr> x_equals_2 = EqExpr::create(read_x, two);

  ref<Expr> read_y = Expr::createTempRead(y_array, 32);
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

} // end anonymous namespace
