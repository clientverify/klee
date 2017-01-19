//===-- LazyConstraintTest.cpp ----------------------------------*- C++ -*-===//
//
// Unit tests for the LazyConstraint and LazyConstraintDispatcher classes.
//
//===----------------------------------------------------------------------===//

#include "gtest/gtest.h"

static unsigned int p(unsigned int x) {
  return 641 * x;
}

static unsigned int p_inv(unsigned int x) {
  return 6700417 * x;
}

TEST(ProhibitiveFunction, Pof10) {
  EXPECT_EQ(p(10), 6410);
  EXPECT_EQ(p_inv(6410), 10);
}

TEST(ProhibitiveFunction, PThenPinv) {
  unsigned int n = 314159;
  EXPECT_EQ(p_inv(p(n)), n);
}

TEST(ProhibitiveFunction, PinvThenP) {
  unsigned int n = 314159;
  EXPECT_EQ(p(p_inv(n)), n);
}
