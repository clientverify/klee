//===-- ClusterTest.cpp ---------------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include <iostream>
#include "gtest/gtest.h"

#include "cliver/Cluster.h"
#include "cliver/EditDistance.h"

//using namespace klee;
using namespace cliver;
using namespace std;

namespace {

typedef Score<std::string, char, int> StringScore;
typedef EditDistanceRow<StringScore,std::string,int> StringEDR;

class StringDistanceMetric : public DistanceMetric<std::string> {

  void init(std::vector<std::string*>* datalist) {}

  int distance(const std::string* s1, const std::string* s2) {
    StringEDR edr(*s1, *s2);
    return edr.compute_editdistance();
  }
};

typedef Clusterer<std::string, StringDistanceMetric> StringClusterer;

////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////

TEST(ClusterTest, Init) {
  StringClusterer sc(5);
  StringDistanceMetric metric;

  std::string s1, s2, s3, s4, s5, s6, s7, s8;
  std::vector<std::string*> v;

  s1 = "ATest";
  s2 = "ATestB";
  s3 = "CTest";
  s4 = "CTes";
  s5 = "CTestExtra";
  s6 = "CTestSuper";
  s7 = "CTesB";
  s8 = "CTestSup";

  v.push_back(s1);
  v.push_back(s2);
  v.push_back(s3);
  v.push_back(s4);
  v.push_back(s5);
  v.push_back(s6);
  v.push_back(s7);
  v.push_back(s8);
  
  sc.add_data(&v, &metric);

};

//////////////////////////////////////////////////////////////////////////////////

}
