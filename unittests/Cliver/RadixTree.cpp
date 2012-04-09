//===-- RadixTree.cpp -----------------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "gtest/gtest.h"

#include "cliver/RadixTree.h"

#include <string>
#include <iostream>

//using namespace klee;
using namespace cliver;
//using namespace std;


//////////////////////////////////////////////////////////////////////////////////

typedef RadixTree<std::string, char> StringRadixTree;

//////////////////////////////////////////////////////////////////////////////////

namespace {

TEST(RadixTreeTest, CreateAndDestroy) {
  StringRadixTree *rt = new StringRadixTree();
  delete rt;
}

TEST(RadixTreeTest, Insert) {
  StringRadixTree *rt = new StringRadixTree();
  std::string s1("ATestString");
  std::string s2("ATestBString");
  std::string s3("");
  std::string s4("CTestString");
  std::string s5("CTestStringExtra");
  std::string s6("CTestStringSuper");
  rt->insert(s1);
  rt->insert(s2);
  rt->insert(s3);
  rt->insert(s4);
  rt->insert(s5);
  rt->insert(s6);
}

TEST(RadixTreeTest, Lookup) {
  StringRadixTree *rt = new StringRadixTree();
  std::string s1("ATestString");
  std::string s2("ATestBString");
  std::string s3("");
  std::string s4("ATestBStr");
  std::string s5("ATestBStrings");
  EXPECT_EQ(rt->lookup(s1), false);
  rt->insert(s1);
  EXPECT_EQ(rt->lookup(s2), false);
  rt->insert(s2);
  EXPECT_EQ(rt->lookup(s1), true);
  EXPECT_EQ(rt->lookup(s2), true);
  rt->insert(s3);
  EXPECT_EQ(rt->lookup(s3), true);

  EXPECT_EQ(rt->lookup(s4), true);
  EXPECT_EQ(rt->lookup(s5), false);
}

TEST(RadixTreeTest, Remove) {
  StringRadixTree *rt = new StringRadixTree();
  std::string s1("ATestString");
  std::string s2("ATestBString");
  std::string s3("");
  std::string s4("ATestBStr");
  std::string s5("ATestBStrings");
  EXPECT_EQ(rt->lookup(s1), false);
  rt->insert(s1);
  EXPECT_EQ(rt->lookup(s2), false);
  rt->insert(s2);
  EXPECT_EQ(rt->lookup(s1), true);
  EXPECT_EQ(rt->lookup(s2), true);
  rt->insert(s3);
  EXPECT_EQ(rt->lookup(s3), true);

  EXPECT_EQ(rt->lookup(s4), true);
  EXPECT_EQ(rt->lookup(s5), false);
}

//////////////////////////////////////////////////////////////////////////////////

}
