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
#include "cliver/EditDistance.h"

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

typedef EditDistanceTable<Score<string,int>,string,int> StringEDT;
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
  ClientVerifier *cv = new ClientVerifier();
  delete cv;
}

////////////////////////////////////////////////////////////////////////////////

TEST(CliverTest, EditDistance1) {
  StringEDT edt(test1.s, test1.t);
  edt.compute_editdistance();

  ASSERT_EQ(test1.costs, edt.costs());
}

TEST(CliverTest, EditDistance2) {
  StringEDT edt(test2.s, test2.t);
  edt.compute_editdistance();

  ASSERT_EQ(test2.costs, edt.costs());
}

TEST(CliverTest, EditDistance3) {
  StringEDT edt(test3.s, test3.t);
  edt.compute_editdistance();

  ASSERT_EQ(test3.costs, edt.costs());
}

////////////////////////////////////////////////////////////////////////////////

}
