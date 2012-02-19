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
    : s(_s), t(_t) {
    for (unsigned i=0; i<(s.size()+1)*(t.size()+1); ++i)
      costs.push_back(_costs[i]);
  }
  SequenceType s;
  SequenceType t;
  std::vector<ValueType> costs;
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

////////////////////////////////////////////////////////////////////////////////

TEST(CliverTest, ClientVerifierConstructionDestruction) {
  // Don't create output directory!
  NoOutputFlag = true;
  std::string test_filename("test.bc");
  ClientVerifier *cv = new ClientVerifier(&test_filename);
  delete cv;
}

////////////////////////////////////////////////////////////////////////////////

TEST(CliverTest, EditDistanceAB) {
  std::string s = "AAAAABBBBB";
  std::string t = "BBBBBCCCCC";
  StringEDT edt(s, t);
  StringEDR edr(s, t);
  StringEDU edu(s, t);
  //StringEDUKK edukk(s, t);
  //StringEDDynamicUKK eddukk(s, t);
  //StringEDFullUKK edsukk(s, t);

  int cost_t = edt.compute_editdistance();
  int cost_r = edr.compute_editdistance();
  int cost_u = edu.compute_editdistance();
  //int cost_ukk = edukk.compute_editdistance();
  //int cost_dukk = eddukk.compute_editdistance();
  //int cost_sukk = edsukk.compute_editdistance();

  ASSERT_EQ(cost_t, cost_r);
  ASSERT_EQ(cost_t, cost_u);
  //ASSERT_EQ(cost_t, cost_ukk);
  //ASSERT_EQ(cost_t, cost_dukk);
  //ASSERT_EQ(cost_t, cost_sukk);
}


TEST(CliverTest, EditDistance0) {
  std::string s = "ACCGGTCGGC";
  std::string t = "TGGTCGCCC";
  StringEDT edt(s, t);
  StringEDR edr(s, t);
  StringEDU edu(s, t);
  StringEDUKK edukk(s, t);
  StringEDDynamicUKK eddukk(s, t);
  StringEDFullUKK edsukk(s, t);

  int cost_t = edt.compute_editdistance();
  int cost_r = edr.compute_editdistance();
  int cost_u = edu.compute_editdistance();
  int cost_ukk = edukk.compute_editdistance();
  int cost_dukk = eddukk.compute_editdistance();
  int cost_sukk = edsukk.compute_editdistance();

  ASSERT_EQ(cost_t, cost_r);
  ASSERT_EQ(cost_t, cost_u);
  ASSERT_EQ(cost_t, cost_ukk);
  ASSERT_EQ(cost_t, cost_dukk);
  ASSERT_EQ(cost_t, cost_sukk);
}

TEST(CliverTest, EditDistance1) {
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

  ASSERT_EQ(cost_t, cost_r);
  ASSERT_EQ(cost_t, cost_u);
  ASSERT_EQ(cost_t, cost_ukk);
  ASSERT_EQ(cost_t, cost_dukk);
  ASSERT_EQ(cost_t, cost_sukk);
}

TEST(CliverTest, EditDistance2) {
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

  ASSERT_EQ(cost_t, cost_r);
  ASSERT_EQ(cost_t, cost_u);
  ASSERT_EQ(cost_t, cost_ukk);
  ASSERT_EQ(cost_t, cost_dukk);
  ASSERT_EQ(cost_t, cost_sukk);
}

TEST(CliverTest, EditDistanceUKK) {
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

  ASSERT_EQ(cost_t, cost_r);
  ASSERT_EQ(cost_t, cost_u);
  ASSERT_EQ(cost_t, cost_ukk);
  ASSERT_EQ(cost_t, cost_dukk);
  ASSERT_EQ(cost_t, cost_sukk);
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
