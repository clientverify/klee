//===-- ExprTest.cpp ------------------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include <iostream>
#include "gtest/gtest.h"

#include "klee/Expr.h"
#include "cliver/ClientVerifier.h"

using namespace klee;
using namespace cliver;

namespace {

TEST(CliverTest, ClientVerifierConstructionDestruction) {
  ClientVerifier *cv = new ClientVerifier();
  delete cv;
}
}
