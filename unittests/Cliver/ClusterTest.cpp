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

#include <vector>
#include <string>


#include <boost/unordered_map.hpp>

using namespace cliver;
using namespace std;

////////////////////////////////////////////////////////////////////////////////

namespace {

////////////////////////////////////////////////////////////////////////////////

#include "RadixTree.inc"

typedef Score<std::string, char, int> StringScore;
typedef EditDistanceRow<StringScore,std::string,int> StringEDR;


class MemoStringDistanceMetric : public cliver::DistanceMetric<std::string> {
 public:
  typedef std::pair<const std::string*, const std::string*> StringPtrPair;
  typedef boost::unordered_map<StringPtrPair, double> StringPtrPairDistanceMap;

  void init(std::vector<std::string*> &datalist) {}

  double distance(const std::string* s1, const std::string* s2) {
    StringPtrPair str_pair(std::min(s1,s2), std::max(s1,s2));

    if (distance_map_.count(str_pair))
      return distance_map_[str_pair];

    StringEDR edr(*s1, *s2);
    double distance = (double)edr.compute_editdistance();
    distance_map_[str_pair] = distance;
    return distance;
  }
 private:
  StringPtrPairDistanceMap distance_map_;

};

class StringDistanceMetric : public cliver::DistanceMetric<std::string> {
 public:
  void init(std::vector<std::string*> &datalist) {}

  double distance(const std::string* s1, const std::string* s2) {
    StringEDR edr(*s1, *s2);
    return (double)edr.compute_editdistance();
  }

};


typedef Clusterer<std::string, StringDistanceMetric> StringClusterer;

////////////////////////////////////////////////////////////////////////////////

std::vector<std::string> dictionary;

// Helper to return end of array
template<typename T, size_t N> T * end(T (&ra)[N]) { return ra + N; }

////////////////////////////////////////////////////////////////////////////////

template<class ClustererType>
//class PClustererTest: public ::testing::TestWithParam<int> {
class ClustererTest : public ::testing::Test {
 protected:

  virtual void SetUp() {
    c_ = new ClustererType();
  }

  void SetupDictionary() {
    size_t count = 100;
    if (dictionary.empty())
      dictionary = std::vector<std::string>(cstr_dictionary, end(cstr_dictionary));
    //for (unsigned i=0; i<dictionary.size(); ++i) {
    for (unsigned i=0; i<100; ++i) {
      size_t r = rand();

      typename ClustererType::data_type* v = 
        new typename ClustererType::data_type(dictionary[r%dictionary.size()].begin(), 
                                              dictionary[r%dictionary.size()].end());
      v_dictionary.push_back(v);
      //typename ClustererType::data_type* v = 
      //  new typename ClustererType::data_type(dictionary[i].begin(), 
      //                                        dictionary[i].end());
      //v_dictionary.push_back(v);
    }
  }

  void InsertDictionary() {
    this->SetupDictionary();
    c_->add_data(v_dictionary);

    //for (unsigned i=0; i<v_dictionary.size(); ++i) {
    //  rt_->insert(v_dictionary[i]);
    //}
  }

  virtual void TearDown() {
    delete c_;
    // todo delete all dictionary strings
    for (unsigned i=0; i<v_dictionary.size(); ++i) {
      delete v_dictionary[i];
    }
  }

  ClustererType* c_;
  std::vector<typename ClustererType::data_type*> v_dictionary;
};


////////////////////////////////////////////////////////////////////////////////

using ::testing::Types;

typedef Types<
  StringClusterer
> ClustererTypes;

TYPED_TEST_CASE(ClustererTest, ClustererTypes);

TYPED_TEST(ClustererTest, Init) {
  StringDistanceMetric metric;
  this->c_->init(4, &metric);
}

TYPED_TEST(ClustererTest, AddData) {
  StringDistanceMetric metric;
  this->c_->init(4, &metric);
  this->InsertDictionary();
}

TYPED_TEST(ClustererTest, DoCluster) {
  StringDistanceMetric metric;
  this->c_->init(25, &metric);
  this->InsertDictionary();
  this->c_->cluster();
  this->c_->print_clusters();
}

//INSTANTIATE_TEST_CASE_P(ClusterSizes, PClustererTest,
//                        ::testing::Values(2,4));


////////////////////////////////////////////////////////////////////////////////
/*
TEST(ClusterTest, Init) {
  StringClusterer sc;
  StringDistanceMetric metric;
  sc.init(5, &metric);
}

TEST(ClusterTest, AddData) {
  StringClusterer sc;
  StringDistanceMetric metric;

  sc.init(2, &metric);
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

  v.push_back(&s1);
  v.push_back(&s2);
  v.push_back(&s3);
  v.push_back(&s4);
  v.push_back(&s5);
  v.push_back(&s6);
  v.push_back(&s7);
  v.push_back(&s8);
  
  sc.add_data(v);
}

TEST(ClusterTest, DoCluster) {
  StringClusterer sc;
  StringDistanceMetric metric;

  sc.init(2, &metric);
  std::string s1, s2, s3, s4, s5, s6, s7, s8;
  std::vector<std::string*> v;

  s1 = "Apple";
  s2 = "AppLe";
  s3 = "apple";
  s4 = "crapple";
  s5 = "worm";
  s6 = "Worm";
  s7 = "worms";
  s8 = "wormy";

  v.push_back(&s1);
  v.push_back(&s2);
  v.push_back(&s3);
  v.push_back(&s4);
  v.push_back(&s5);
  v.push_back(&s6);
  v.push_back(&s7);
  v.push_back(&s8);
  
  sc.add_data(v);
  sc.cluster();
}
*/



//////////////////////////////////////////////////////////////////////////////////

}
