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
#include "cliver/ExecutionTree.h"
#include "cliver/EditDistance.h"
#include "cliver/CVStream.h"

#include "llvm/Analysis/Trace.h"
#include "llvm/BasicBlock.h"
#include "llvm/LLVMContext.h"

#include <string>
#include <iostream>

using namespace klee;
using namespace cliver;
using namespace std;

namespace {

////////////////////////////////////////////////////////////////////////////////

template <class SequenceType, class ValueType>
struct EditDistanceTableData {
  EditDistanceTableData(SequenceType _s, SequenceType _t, ValueType* _costs)
    : s(_s), t(_t), m(s.size()+1), n(t.size()+1), costs(_costs) {}
  ValueType cost(int i, int j) { return costs[(m*j) + i]; } 
  ValueType edit_distance() { return costs[(m*n) - 1]; }
  SequenceType s;
  SequenceType t;
  int m, n;
  ValueType* costs;
};

typedef Score<std::string, char, int> StringScore;
typedef EditDistanceTable<StringScore,string,int> StringEDT;
typedef EditDistanceRow<StringScore,string,int> StringEDR;
typedef EditDistanceUkkonen<StringScore,string,int> StringEDU;
typedef EditDistanceUKK<StringScore,string,int> StringEDUKK;
typedef EditDistanceDynamicUKK<StringScore,string,int> StringEDDynamicUKK;
typedef EditDistanceStaticUKK<StringScore,string,int> StringEDStaticUKK;
typedef EditDistanceFullUKK<StringScore,string,int> StringEDFullUKK;

typedef EditDistanceTableData<std::string, int> StringEDTData;

std::ostream& operator<<(std::ostream &os, const std::vector<int> &v) {
  for (unsigned i=0; i<v.size(); ++i)
    os << v[i] << ", ";
  return os;
}

////////////////////////////////////////////////////////////////////////////////

int test1_costs[] = {
0, 1, 2, 3, 4, 5, 6, 
1, 1, 2, 3, 4, 5, 6, 
2, 2, 1, 2, 3, 4, 5, 
3, 3, 2, 1, 2, 3, 4, 
4, 4, 3, 2, 1, 2, 3, 
5, 5, 4, 3, 2, 2, 3, 
6, 6, 5, 4, 3, 3, 2, 
7, 7, 6, 5, 4, 4, 3
};
StringEDTData test1("kitten","sitting",test1_costs);

int test2_costs[] = {
0, 1, 2, 3, 4, 5, 6, 7, 8, 
1, 0, 1, 2, 3, 4, 5, 6, 7, 
2, 1, 1, 2, 2, 3, 4, 5, 6, 
3, 2, 2, 2, 3, 3, 4, 5, 6, 
4, 3, 3, 3, 3, 4, 3, 4, 5, 
5, 4, 3, 4, 4, 4, 4, 3, 4, 
6, 5, 4, 4, 5, 5, 5, 4, 3
};
StringEDTData test2("Saturday","Sunday",test2_costs);

int test3_costs[] = {
 0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14,
 1,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 13,
 2,  2,  2,  3,  4,  5,  5,  6,  7,  8,  9, 10, 11, 12, 13,
 3,  3,  2,  3,  4,  5,  6,  5,  6,  7,  8,  9, 10, 11, 12,
 4,  4,  3,  2,  3,  4,  5,  6,  5,  6,  7,  8,  9, 10, 11,
 5,  5,  4,  3,  2,  3,  4,  5,  6,  6,  7,  7,  8,  9, 10,
 6,  6,  5,  4,  3,  2,  3,  4,  5,  6,  7,  8,  8,  9, 10,
 7,  7,  6,  5,  4,  3,  2,  3,  4,  5,  6,  7,  8,  9, 10,
 8,  8,  7,  6,  5,  4,  3,  3,  4,  5,  6,  7,  8,  9, 10,
 9,  9,  8,  7,  6,  5,  4,  4,  4,  4,  5,  6,  7,  8,  9,
10, 10,  9,  8,  7,  6,  5,  5,  5,  5,  5,  6,  7,  8,  8,
11, 11, 10,  9,  8,  7,  6,  6,  6,  6,  6,  5,  6,  7,  8,
12, 12, 11, 10,  9,  8,  7,  7,  7,  7,  7,  6,  5,  6,  7,
13, 13, 12, 11, 10,  9,  8,  7,  8,  8,  8,  7,  6,  5,  6,
14, 14, 13, 12, 11, 10,  9,  8,  8,  9,  9,  8,  7,  6,  5
};
StringEDTData test3("you-should-not", "thou-shalt-not", test3_costs);

int test4_costs[] = {
0, 1, 2, 3, 4, 5, 6, 7, 8, 9,
1, 1, 2, 3, 4, 5, 6, 7, 8, 9,
2, 2, 2, 3, 4, 4, 5, 6, 7, 8,
3, 3, 3, 3, 4, 4, 5, 5, 6, 7,
4, 4, 3, 3, 4, 5, 4, 5, 6, 7,
5, 5, 4, 3, 4, 5, 5, 5, 6, 7,
6, 5, 5, 4, 3, 4, 5, 6, 6, 7,
7, 6, 6, 5, 4, 3, 4, 5, 6, 6,
8, 7, 6, 6, 5, 4, 3, 4, 5, 6,
9, 8, 7, 6, 6, 5, 4, 4, 5, 6,
10, 9, 8, 7, 7, 6, 5, 4, 4, 5
};
StringEDTData test4("TGGTCGCCC","ACCGGTCGGC",test4_costs);

////////////////////////////////////////////////////////////////////////////////

TEST(CliverTest, EditDistanceTest0) {
  std::string s = "AAAAABBBBB";
  std::string t = "BBBBBCCCCC";
  int edit_distance = 10;

  StringEDT edt(s, t);
  StringEDR edr(s, t);
  StringEDU edu(s, t);
  StringEDUKK edukk(s, t);
  StringEDDynamicUKK eddukk(s, t);
  //StringEDFullUKK edsukk(s, t);

  int cost_t = edt.compute_editdistance();
  int cost_r = edr.compute_editdistance();
  int cost_u = edu.compute_editdistance();
  int cost_ukk = edukk.compute_editdistance();
  int cost_dukk = eddukk.compute_editdistance();
  //int cost_sukk = edsukk.compute_editdistance();

  EXPECT_EQ(edit_distance, cost_t);
  EXPECT_EQ(edit_distance, cost_r);
  EXPECT_EQ(edit_distance, cost_u);
  EXPECT_EQ(edit_distance, cost_ukk);
  EXPECT_EQ(edit_distance, cost_dukk);
  //EXPECT_EQ(edit_distance, cost_sukk);
}

#if 0
TEST(CliverTest, EditDistanceTest_AA) {
  std::string s = "A";
  std::string t = "A";
  int edit_distance = 0;

  StringEDT edt(s, t);
  StringEDR edr(s, t);
  StringEDU edu(s, t);
  StringEDUKK edukk(s, t);
  StringEDDynamicUKK eddukk(s, t);
  //StringEDFullUKK edsukk(s, t);

  int cost_t = edt.compute_editdistance();
  int cost_r = edr.compute_editdistance();
  int cost_u = edu.compute_editdistance();
  int cost_ukk = edukk.compute_editdistance();
  int cost_dukk = eddukk.compute_editdistance();
  //int cost_sukk = edsukk.compute_editdistance();

  EXPECT_EQ(edit_distance, cost_t);
  EXPECT_EQ(edit_distance, cost_r);
  EXPECT_EQ(edit_distance, cost_u);
  EXPECT_EQ(edit_distance, cost_ukk);
  EXPECT_EQ(edit_distance, cost_dukk);
  //EXPECT_EQ(edit_distance, cost_sukk);
}
#endif

TEST(CliverTest, EditDistanceTest_AB) {
  std::string s = "A";
  std::string t = "B";
  int edit_distance = 1;

  StringEDT edt(s, t);
  StringEDR edr(s, t);
  StringEDU edu(s, t);
  StringEDUKK edukk(s, t);
  StringEDDynamicUKK eddukk(s, t);
  //StringEDFullUKK edsukk(s, t);

  int cost_t = edt.compute_editdistance();
  int cost_r = edr.compute_editdistance();
  int cost_u = edu.compute_editdistance();
  int cost_ukk = edukk.compute_editdistance();
  int cost_dukk = eddukk.compute_editdistance();
  //int cost_sukk = edsukk.compute_editdistance();

  EXPECT_EQ(edit_distance, cost_t);
  EXPECT_EQ(edit_distance, cost_r);
  EXPECT_EQ(edit_distance, cost_u);
  EXPECT_EQ(edit_distance, cost_ukk);
  EXPECT_EQ(edit_distance, cost_dukk);
  //EXPECT_EQ(edit_distance, cost_sukk);
}


TEST(CliverTest, EditDistanceTest1) {
  StringEDT edt(test1.s, test1.t);
  StringEDR edr(test1.s, test1.t);
  StringEDU edu(test1.s, test1.t);
  StringEDUKK edukk(test1.s, test1.t);
  StringEDDynamicUKK eddukk(test1.s, test1.t);
  StringEDFullUKK edsukk(test1.s, test1.t);

  int cost_t = edt.compute_editdistance();
  int cost_r = edr.compute_editdistance();
  int cost_u = edu.compute_editdistance();
  int cost_ukk = edukk.compute_editdistance();
  int cost_dukk = eddukk.compute_editdistance();
  int cost_sukk = edsukk.compute_editdistance();

  EXPECT_EQ(test1.edit_distance(), cost_t);
  EXPECT_EQ(test1.edit_distance(), cost_r);
  EXPECT_EQ(test1.edit_distance(), cost_u);
  EXPECT_EQ(test1.edit_distance(), cost_ukk);
  EXPECT_EQ(test1.edit_distance(), cost_dukk);
  EXPECT_EQ(test1.edit_distance(), cost_sukk);
}

TEST(CliverTest, EditDistanceTest2) {
  StringEDT edt(test2.s, test2.t);
  StringEDR edr(test2.s, test2.t);
  StringEDU edu(test2.s, test2.t);
  StringEDUKK edukk(test2.s, test2.t);
  StringEDDynamicUKK eddukk(test2.s, test2.t);
  StringEDFullUKK edsukk(test2.s, test2.t);

  int cost_t = edt.compute_editdistance();
  int cost_r = edr.compute_editdistance();
  int cost_u = edu.compute_editdistance();
  int cost_ukk = edukk.compute_editdistance();
  int cost_dukk = eddukk.compute_editdistance();
  int cost_sukk = edsukk.compute_editdistance();

  EXPECT_EQ(test2.edit_distance(), cost_t);
  EXPECT_EQ(test2.edit_distance(), cost_r);
  EXPECT_EQ(test2.edit_distance(), cost_u);
  EXPECT_EQ(test2.edit_distance(), cost_ukk);
  EXPECT_EQ(test2.edit_distance(), cost_dukk);
  EXPECT_EQ(test2.edit_distance(), cost_sukk);
}

TEST(CliverTest, EditDistanceTest3) {
  StringEDT edt(test3.s, test3.t);
  StringEDR edr(test3.s, test3.t);
  StringEDU edu(test3.s, test3.t);
  StringEDUKK edukk(test3.s, test3.t);
  StringEDDynamicUKK eddukk(test3.s, test3.t);
  StringEDFullUKK edsukk(test3.s, test3.t);

  int cost_t = edt.compute_editdistance();
  int cost_r = edr.compute_editdistance();
  int cost_u = edu.compute_editdistance();
  int cost_ukk = edukk.compute_editdistance();
  int cost_dukk = eddukk.compute_editdistance();
  int cost_sukk = edsukk.compute_editdistance();

  EXPECT_EQ(test3.edit_distance(), cost_t);
  EXPECT_EQ(test3.edit_distance(), cost_r);
  EXPECT_EQ(test3.edit_distance(), cost_u);
  EXPECT_EQ(test3.edit_distance(), cost_ukk);
  EXPECT_EQ(test3.edit_distance(), cost_dukk);
  EXPECT_EQ(test3.edit_distance(), cost_sukk);
}

TEST(CliverTest, EditDistanceTest4) {
  StringEDT edt(test4.s, test4.t);
  StringEDR edr(test4.s, test4.t);
  StringEDU edu(test4.s, test4.t);
  StringEDUKK edukk(test4.s, test4.t);
  StringEDDynamicUKK eddukk(test4.s, test4.t);
  StringEDFullUKK edsukk(test4.s, test4.t);

  int cost_t = edt.compute_editdistance();
  int cost_r = edr.compute_editdistance();
  int cost_u = edu.compute_editdistance();
  int cost_ukk = edukk.compute_editdistance();
  int cost_dukk = eddukk.compute_editdistance();
  int cost_sukk = edsukk.compute_editdistance();

  EXPECT_EQ(test4.edit_distance(), cost_t);
  EXPECT_EQ(test4.edit_distance(), cost_r);
  EXPECT_EQ(test4.edit_distance(), cost_u);
  EXPECT_EQ(test4.edit_distance(), cost_ukk);
  EXPECT_EQ(test4.edit_distance(), cost_dukk);
  EXPECT_EQ(test4.edit_distance(), cost_sukk);
}

//TEST(CliverTest, EditDistance2) {
//  StringEDT edt(test2.s, test2.t);
//  StringEDR edr(test2.s, test2.t);
//
//  ASSERT_EQ(edt.compute_editdistance(), edr.compute_editdistance());
//  ASSERT_EQ(test2.costs, edt.costs());
//}
//
//TEST(CliverTest, EditDistance3) {
//  StringEDT edt(test3.s, test3.t);
//  StringEDR edr(test3.s, test3.t);
//
//  ASSERT_EQ(edt.compute_editdistance(), edr.compute_editdistance());
//  ASSERT_EQ(test3.costs, edt.costs());
//}
//
//////////////////////////////////////////////////////////////////////////////////
//
//TEST(CliverTest, ExecutionTree) {
//  ExecutionTrace::BasicBlockList bb_list;
//  ExecutionTrace etrace1, etrace2;
//
//  etrace2.push_back_bb(
//      new klee::KBasicBlock(
//        llvm::BasicBlock::Create(llvm::getGlobalContext()), 5));
//
//  for(int i=0;i<10;i++) {
//    bb_list.push_back(
//      new klee::KBasicBlock(
//        llvm::BasicBlock::Create(llvm::getGlobalContext()), i));
//    etrace1.push_back_bb(bb_list.back());
//  }
//
//  etrace2.push_back(etrace1);
//  ASSERT_EQ(11, etrace2.size());
//
//}

}
