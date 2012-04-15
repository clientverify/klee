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
#include "cliver/ExecutionTrace.h"

#include <string>
#include <iostream>

//using namespace klee;
using namespace cliver;
//using namespace std;


//////////////////////////////////////////////////////////////////////////////////

typedef RadixTree<std::string, char> StringRadixTree;
typedef RadixTree<std::vector<char>, char> VectorRadixTree;
typedef RadixTree<ExecutionTrace, ExecutionTrace::ID> TraceRadixTree;

//////////////////////////////////////////////////////////////////////////////////

class RadixTreeTest : public ::testing::Test {
 protected:

  virtual void SetUp() {
    srt = new StringRadixTree();
    vrt = new VectorRadixTree();

    s1 = "ATest";
    s2 = "ATestB";
    s3 = "CTest";
    s4 = "CTes";
    s5 = "CTestExtra";
    s6 = "CTestSuper";
    s7 = "CTesB";
    s8 = "CTestSup";

    v1.insert(v1.end(), s1.begin(), s1.end());
    v2.insert(v2.end(), s2.begin(), s2.end());
    v3.insert(v3.end(), s3.begin(), s3.end());
    v4.insert(v4.end(), s4.begin(), s4.end());
    v5.insert(v5.end(), s5.begin(), s5.end());
    v6.insert(v6.end(), s6.begin(), s6.end());
    v7.insert(v7.end(), s7.begin(), s7.end());
    v8.insert(v8.end(), s8.begin(), s8.end());
  }

  void InsertAll() {
    srt->insert(s_empty);
    srt->insert(s1);
    srt->insert(s2);
    srt->insert(s3);
    srt->insert(s4);
    srt->insert(s5);
    srt->insert(s6);
    srt->insert(s7);
    srt->insert(s8);

    vrt->insert(v_empty);
    vrt->insert(v1);
    vrt->insert(v2);
    vrt->insert(v3);
    vrt->insert(v4);
    vrt->insert(v5);
    vrt->insert(v6);
    vrt->insert(v7);
    vrt->insert(v8);
  }

  virtual void TearDown() {
    delete srt;
    delete vrt;
  }

  StringRadixTree* srt;
  VectorRadixTree* vrt;

  std::string s_empty, s1, s2, s3, s4, s5, s6, s7, s8;

  std::vector<char> v_empty, v1, v2, v3, v4, v5, v6, v7, v8;

};


//////////////////////////////////////////////////////////////////////////////////

namespace {

TEST_F(RadixTreeTest, InitExecutionTrace) {
  TraceRadixTree *trt = new TraceRadixTree();
  TraceRadixTree::Node* n = trt->extend(10);
  n = trt->extend(12, n);
  n = trt->extend(16, n);
  n = trt->extend(19, n);
  n = trt->extend(1, n);
  ExecutionTrace et;
  trt->get(n, et);
  EXPECT_EQ(et.size(), 5);
  delete trt;
}

TEST_F(RadixTreeTest, Init) {
  ASSERT_TRUE(srt != NULL);
  ASSERT_TRUE(vrt != NULL);
}

TEST_F(RadixTreeTest, Insert) {
  InsertAll();
}

TEST_F(RadixTreeTest, Lookup) {
  EXPECT_EQ(srt->lookup(s1), false);
  srt->insert(s1);
  EXPECT_EQ(srt->lookup(s2), false);
  srt->insert(s2);
  EXPECT_EQ(srt->lookup(s1), true);
  EXPECT_EQ(srt->lookup(s2), true);
  srt->insert(s3);
  EXPECT_EQ(srt->lookup(s3), true);
  EXPECT_EQ(srt->lookup(s4), true);
  EXPECT_EQ(srt->lookup(s5), false);
  EXPECT_EQ(srt->lookup(s7), false);
  EXPECT_EQ(vrt->lookup(v1), false);
  vrt->insert(v1);
  EXPECT_EQ(vrt->lookup(v2), false);
  vrt->insert(v2);
  EXPECT_EQ(vrt->lookup(v1), true);
  EXPECT_EQ(vrt->lookup(v2), true);
  vrt->insert(v3);
  EXPECT_EQ(vrt->lookup(v3), true);
  EXPECT_EQ(vrt->lookup(v4), true);
  EXPECT_EQ(vrt->lookup(v5), false);
}

TEST_F(RadixTreeTest, Remove) {
  InsertAll();

  EXPECT_EQ(srt->lookup(s1), true);
  EXPECT_EQ(srt->remove(s2), true);
  EXPECT_EQ(srt->lookup(s2), false);
  EXPECT_EQ(srt->remove(s8), false);
  EXPECT_EQ(srt->lookup(s8), true);
  EXPECT_EQ(srt->lookup(s6), true);
  EXPECT_EQ(srt->remove(s6), true);
  EXPECT_EQ(srt->lookup(s6), false);
  EXPECT_EQ(vrt->lookup(v1), true);
  EXPECT_EQ(vrt->remove(v2), true);
  EXPECT_EQ(vrt->lookup(v2), false);
  EXPECT_EQ(vrt->remove(v8), false);
  EXPECT_EQ(vrt->lookup(v8), true);
  EXPECT_EQ(vrt->lookup(v6), true);
  EXPECT_EQ(vrt->remove(v6), true);
  EXPECT_EQ(vrt->lookup(v6), false);
}

//////////////////////////////////////////////////////////////////////////////////

}
