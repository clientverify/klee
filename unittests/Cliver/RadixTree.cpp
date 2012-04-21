//===-- RadixTree.cpp -----------------------------------------------------===//
//
//===----------------------------------------------------------------------===//

#include "gtest/gtest.h"

#include "cliver/RadixTree.h"
#include "cliver/TrackingRadixTree.h"
#include "cliver/LevenshteinRadixTree.h"
#include "cliver/EditDistance.h"

#include <stdlib.h>
#include <string>
#include <iostream>

using namespace cliver;

#include "RadixTree.inc"

// Helper to return end of array
template<typename T, size_t N> T * end(T (&ra)[N]) { return ra + N; }

//////////////////////////////////////////////////////////////////////////////////
// Under test: RadixTree and RadixTree inheriting classes 
//////////////////////////////////////////////////////////////////////////////////

typedef RadixTree<std::string, char> StringRadixTree;
typedef RadixTree<std::vector<char>, char> VectorRadixTree;
typedef LevenshteinRadixTree<std::string, char> StringLevenshteinRadixTree;

struct TrackingObject {
  unsigned id;
  std::string str;
};

typedef TrackingRadixTree<std::string, char, TrackingObject> StringTrackingRadixTree;

//////////////////////////////////////////////////////////////////////////////////
// EditDistance typedefs for verifiying LevenshteinRadixTree, this classes are
// not tested here

typedef Score<std::string, char, int> StringScore;
typedef EditDistanceTable<StringScore,std::string,int> StringEDT;
typedef EditDistanceRow<StringScore,std::string,int> StringEDR;

//////////////////////////////////////////////////////////////////////////////////

std::vector<std::string> s_dictionary;
std::vector<std::vector<char> > v_dictionary;

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

  static void SetUpTestCase() {
    if (s_dictionary.empty())
      s_dictionary = std::vector<std::string>(cstr_dictionary, end(cstr_dictionary));

    if (v_dictionary.empty()) {
      for (int i=0; i<s_dictionary.size(); ++i) {
        std::vector<char> v(s_dictionary[i].begin(), s_dictionary[i].end());
        v_dictionary.push_back(v);
      }
    }
  }

  void InsertDictionary() {

    for (int i=0; i<s_dictionary.size(); ++i) {
      srt->insert(s_dictionary[i]);
    }

    for (int i=0; i<v_dictionary.size(); ++i) {
      vrt->insert(v_dictionary[i]);
    }
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

#if 1

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

TEST_F(RadixTreeTest, Clone) {
  InsertAll();

  StringRadixTree *clone_srt = srt->clone();
  delete srt;
  EXPECT_EQ(clone_srt->lookup(s1), true);
  EXPECT_EQ(clone_srt->remove(s2), true);
  EXPECT_EQ(clone_srt->lookup(s2), false);
  EXPECT_EQ(clone_srt->remove(s8), false);
  EXPECT_EQ(clone_srt->lookup(s8), true);
  EXPECT_EQ(clone_srt->lookup(s6), true);
  EXPECT_EQ(clone_srt->remove(s6), true);
  EXPECT_EQ(clone_srt->lookup(s6), false);
  delete clone_srt;
  srt = new StringRadixTree();

  VectorRadixTree *clone_vrt = vrt->clone();
  delete vrt;
  EXPECT_EQ(clone_vrt->lookup(v1), true);
  EXPECT_EQ(clone_vrt->remove(v2), true);
  EXPECT_EQ(clone_vrt->lookup(v2), false);
  EXPECT_EQ(clone_vrt->remove(v8), false);
  EXPECT_EQ(clone_vrt->lookup(v8), true);
  EXPECT_EQ(clone_vrt->lookup(v6), true);
  EXPECT_EQ(clone_vrt->remove(v6), true);
  EXPECT_EQ(clone_vrt->lookup(v6), false);
  delete clone_vrt;
  vrt = new VectorRadixTree();

}

TEST_F(RadixTreeTest, InsertDictionary) {
  InsertDictionary();
}

TEST_F(RadixTreeTest, LookupDictionary) {
  InsertDictionary();
  for (int i = 0; i<s_dictionary.size(); ++i) {
    EXPECT_EQ(srt->lookup(s_dictionary[i]), true);
  }
}

TEST_F(RadixTreeTest, RemoveDictionary) {
  InsertDictionary();
  
  srand(0);
  int r = 0, freq = 10;
  while (r < s_dictionary.size()) {
    bool result = srt->remove(s_dictionary[r]);
    ASSERT_EQ(srt->lookup(s_dictionary[r]), !result);
    r += 1 + (rand() % freq);
  }

  srand(0);
  r = 0;
  while (r < v_dictionary.size()) {
    bool result = vrt->remove(v_dictionary[r]);
    ASSERT_EQ(vrt->lookup(v_dictionary[r]), !result);
    r += 1 + (rand() % freq);
  }
}

TEST_F(RadixTreeTest, CloneDictionary) {
  InsertDictionary();

  StringRadixTree *clone_srt = srt->clone();
  delete srt;
  
  srand(0);
  int r = 0, freq = 100;
  while (r < s_dictionary.size()) {
    bool result = clone_srt->remove(s_dictionary[r]);
    ASSERT_EQ(clone_srt->lookup(s_dictionary[r]), !result);
    r += 1 + (rand() % freq);
  }

  delete clone_srt;
  srt = new StringRadixTree();

  VectorRadixTree *clone_vrt = vrt->clone();
  delete vrt;

  srand(0);
  r = 0;
  while (r < v_dictionary.size()) {
    bool result = clone_vrt->remove(v_dictionary[r]);
    ASSERT_EQ(clone_vrt->lookup(v_dictionary[r]), !result);
    r += 1 + (rand() % freq);
  }

  delete clone_vrt;
  vrt = new VectorRadixTree();
}

TEST_F(RadixTreeTest, Levenshtein) {
  StringLevenshteinRadixTree *slrt = new StringLevenshteinRadixTree();
  std::string test("test");
  slrt->min_edit_distance(test);
}

TEST_F(RadixTreeTest, LevenshteinInsert) {
  StringLevenshteinRadixTree *slrt = new StringLevenshteinRadixTree();

  for (int i=0; i<s_dictionary.size(); ++i) {
    slrt->insert(s_dictionary[i]);
  }
  for (int i = 0; i<s_dictionary.size(); ++i) {
    EXPECT_EQ(slrt->lookup(s_dictionary[i]), true);
  }

  std::string test("test");
  slrt->min_edit_distance(test);
}

TEST_F(RadixTreeTest, LevenshteinCloneDictionary) {
  StringLevenshteinRadixTree *slrt = new StringLevenshteinRadixTree();

  for (int i=0; i<s_dictionary.size(); ++i) {
    slrt->insert(s_dictionary[i]);
  }

  StringLevenshteinRadixTree *clone_slrt 
      = static_cast<StringLevenshteinRadixTree*>(slrt->clone());
  delete slrt;
  
  srand(0);
  int r = 0, freq = 100;
  while (r < s_dictionary.size()) {
    bool result = clone_slrt->remove(s_dictionary[r]);
    ASSERT_EQ(clone_slrt->lookup(s_dictionary[r]), !result);
    r += 1 + (rand() % freq);
  }

  delete clone_slrt;
}

TEST_F(RadixTreeTest, LevenshteinComputeVerify) {
  StringLevenshteinRadixTree *slrt = new StringLevenshteinRadixTree();

  std::string kitten = "kitten";
  std::string sitting = "sitting";
  std::string Saturday = "Saturday";
  std::string Sunday = "Sunday";
  std::string Samberg = "Samberg";
  std::string Saturn = "Saturn";
  std::string Macho = "Macho";

  slrt->insert(Saturday);
  slrt->insert(kitten);
  slrt->insert(Samberg);
  slrt->insert(Saturn);
  slrt->insert(Macho);

  StringEDR edr_day(Saturday, Sunday);
  StringEDR edr_cat(kitten, Sunday);
  StringEDR edr_samberg(Samberg, Sunday);
  StringEDR edr_macho(Macho, Sunday);

  int day_cost_r = edr_day.compute_editdistance();
  int cat_cost_r = edr_cat.compute_editdistance();
  int macho_cost_r = edr_macho.compute_editdistance();
  int samberg_cost_r = edr_samberg.compute_editdistance();
  int cost_rt = slrt->min_edit_distance(Sunday);

  EXPECT_EQ(day_cost_r, slrt->lookup_cost(Saturday));
  EXPECT_EQ(cat_cost_r, slrt->lookup_cost(kitten));
  EXPECT_EQ(samberg_cost_r, slrt->lookup_cost(Samberg));
  EXPECT_EQ(macho_cost_r, slrt->lookup_cost(Macho));

  delete slrt;
}
  
TEST_F(RadixTreeTest, LevenshteinComputeRandomVerifyCheck) {
  
  srand(0);
  int r0, r1, count = 5, check = 5;
  for (int i=0; i < count;  ++i) {
    r0 = (rand() % s_dictionary.size());
    for (int j=0; j < check; ++j) {
      r1 = (rand() % s_dictionary.size());
      StringEDR edr(s_dictionary[r1], s_dictionary[r0]);
      int edr_cost = edr.compute_editdistance();
    }
  }
}


TEST_F(RadixTreeTest, LevenshteinComputeRandom) {
  StringLevenshteinRadixTree *slrt = new StringLevenshteinRadixTree();

  for (int i=0; i<s_dictionary.size(); ++i) {
    slrt->insert(s_dictionary[i]);
  }
  
  srand(0);
  int r0, r1, count = 5, check = 5;
  for (int i=0; i < count;  ++i) {
    r0 = (rand() % s_dictionary.size());
    slrt->min_edit_distance(s_dictionary[r0]);
    for (int j=0; j < check; ++j) {
      r1 = (rand() % s_dictionary.size());
      EXPECT_GE(slrt->lookup_cost(s_dictionary[r1]), 0);
    }
  }
  delete slrt;
}


TEST_F(RadixTreeTest, LevenshteinComputeRandomVerify) {
  StringLevenshteinRadixTree *slrt = new StringLevenshteinRadixTree();

  for (int i=0; i<s_dictionary.size(); ++i) {
    slrt->insert(s_dictionary[i]);
  }
  
  srand(0);
  int r0, r1, count = 5, check = 5;
  for (int i=0; i < count;  ++i) {
    r0 = (rand() % s_dictionary.size());
    slrt->min_edit_distance(s_dictionary[r0]);
    for (int j=0; j < check; ++j) {
      r1 = (rand() % s_dictionary.size());
      StringEDR edr(s_dictionary[r1], s_dictionary[r0]);
      int edr_cost = edr.compute_editdistance();
      EXPECT_EQ(edr_cost, slrt->lookup_cost(s_dictionary[r1]));
    }
  }
  delete slrt;
}

TEST_F(RadixTreeTest, LevenshteinComputeRandomVerifyClone) {
  StringLevenshteinRadixTree *slrt = new StringLevenshteinRadixTree();
  StringLevenshteinRadixTree *clone_slrt = slrt;

  for (int i=0; i<s_dictionary.size(); ++i) {
    slrt->insert(s_dictionary[i]);
  }
  
  srand(0);
  int r0, r1, count = 5, check = 5;
  for (int i=0; i < count;  ++i) {
    r0 = (rand() % s_dictionary.size());
    slrt->min_edit_distance(s_dictionary[r0]);
    clone_slrt = static_cast<StringLevenshteinRadixTree*>(slrt->clone());
    delete slrt;
    slrt = clone_slrt;
    for (int j=0; j < check; ++j) {
      r1 = (rand() % s_dictionary.size());
      StringEDR edr(s_dictionary[r1], s_dictionary[r0]);
      int edr_cost = edr.compute_editdistance();
      EXPECT_EQ(edr_cost, clone_slrt->lookup_cost(s_dictionary[r1]));
    }
  }
  delete clone_slrt;
}

TEST_F(RadixTreeTest, LevenshteinComputeRandomVerifyIncrementElement) {
  StringLevenshteinRadixTree *slrt = new StringLevenshteinRadixTree();

  for (int i=0; i<s_dictionary.size(); ++i) {
    slrt->insert(s_dictionary[i]);
  }
  
  srand(0);
  int r0, r1, count = 5, check = 5;
  for (int i=0; i < count;  ++i) {
    r0 = (rand() % s_dictionary.size());
    for (int j=0; j < s_dictionary[r0].size(); j++) {
      slrt->min_edit_distance_suffix(s_dictionary[r0][j]);
    }
    for (int j=0; j < check; ++j) {
      r1 = (rand() % s_dictionary.size());
      StringEDR edr(s_dictionary[r1], s_dictionary[r0]);
      int edr_cost = edr.compute_editdistance();
      EXPECT_EQ(edr_cost, slrt->lookup_cost(s_dictionary[r1]));
    }
    slrt->reset();
  }
  delete slrt;
}

TEST_F(RadixTreeTest, LevenshteinComputeRandomVerifyIncrementSequence) {
  StringLevenshteinRadixTree *slrt = new StringLevenshteinRadixTree();

  for (int i=0; i<s_dictionary.size(); ++i) {
    slrt->insert(s_dictionary[i]);
  }
  
  srand(0);
  int r0, r1, count = 5, check = 5;
  for (int i=0; i < count;  ++i) {
    r0 = (rand() % s_dictionary.size());

    int j_start=0, j_end=0;
    do {
      j_start = j_end;
      j_end = std::min((int)(s_dictionary[r0].size()), (1 + j_end + (rand()%4)));

      std::string str(s_dictionary[r0].begin() + j_start,
                      s_dictionary[r0].begin() + j_end);

      slrt->min_edit_distance_suffix(str);

    } while ((s_dictionary[r0].begin() + j_end) != s_dictionary[r0].end());

    for (int j=0; j < check; ++j) {
      r1 = (rand() % s_dictionary.size());
      StringEDR edr(s_dictionary[r1], s_dictionary[r0]);
      int edr_cost = edr.compute_editdistance();
      EXPECT_EQ(edr_cost, slrt->lookup_cost(s_dictionary[r1]));
    }
    slrt->reset();
  }

  delete slrt;
}
#endif

TEST_F(RadixTreeTest, TrackingRadixTreeExtend) {
  StringTrackingRadixTree *rt = new StringTrackingRadixTree();

  std::vector<TrackingObject*> tracking_objects;

  for (int i=0; i<s_dictionary.size(); ++i) {
    tracking_objects.push_back(new TrackingObject);
    tracking_objects.back()->id = rand();
    tracking_objects.back()->str = s_dictionary[i];
    rt->extend(s_dictionary[i], tracking_objects.back());
  }

  for (int i=0; i<tracking_objects.size(); ++i) {
    std::string test_str;
    EXPECT_EQ(true, rt->tracks(tracking_objects[i]));
    rt->tracker_get(tracking_objects[i], test_str);
    EXPECT_EQ(test_str, tracking_objects[i]->str);
  }

  for (int i=0; i<s_dictionary.size(); ++i) {
    delete tracking_objects[i];
  }
  delete rt;
}

// This is a test of a unoptimized pattern of access where each TrackingObject
// increases in size by one element only each extension and TrackingObjects
// share prefixes as they increase in size.
TEST_F(RadixTreeTest, TrackingRadixTreeExtendElement) {
  StringTrackingRadixTree *rt = new StringTrackingRadixTree();

  std::vector<TrackingObject*> tracking_objects;

  int count = 20000; //s_dictionary.size();
  for (int i=0; i<count; ++i) {
    tracking_objects.push_back(new TrackingObject);
    tracking_objects.back()->id = rand();
    tracking_objects.back()->str = s_dictionary[i];
    for (int j=0; j<s_dictionary[i].size(); ++j) {
      rt->extend(s_dictionary[i][j], tracking_objects.back());
    }
  }

  for (int i=0; i<tracking_objects.size(); ++i) {
    std::string test_str;
    EXPECT_EQ(true, rt->tracks(tracking_objects[i]));
    rt->tracker_get(tracking_objects[i], test_str);
    EXPECT_EQ(test_str, tracking_objects[i]->str);
  }

  for (int i=0; i<tracking_objects.size(); ++i) {
    delete tracking_objects[i];
  }
  delete rt;
}

TEST_F(RadixTreeTest, TrackingRadixTreeExtendAndClone) {
  StringTrackingRadixTree *rt = new StringTrackingRadixTree();

  std::vector<TrackingObject*> tracking_objects;

  for (int i=0; i<s_dictionary.size(); ++i) {
    tracking_objects.push_back(new TrackingObject);
    tracking_objects.back()->id = rand();
    tracking_objects.back()->str = s_dictionary[i];
    rt->extend(s_dictionary[i], tracking_objects.back());
  }

  srand(0);
  int r0, r1, r2, count = 5000;
  for (int i=0; i < count;  ++i) {
    r0 = rand() % s_dictionary.size();
    r1 = rand() % s_dictionary.size();
    r2 = rand() % s_dictionary.size();
    TrackingObject* tobj = tracking_objects[r0];
    TrackingObject* tobj_new = new TrackingObject();
    tracking_objects.push_back(tobj_new);

    EXPECT_EQ(rt->clone_tracker(tobj_new, tobj), true);

    std::string str_ext_1 = "_" + s_dictionary[r1];
    std::string str_ext_2 = "_" + s_dictionary[r2];
    tobj_new->str = tobj->str + str_ext_2;
    tobj->str += str_ext_1;

    rt->extend(str_ext_1, tobj);
    rt->extend(str_ext_2, tobj_new);
  }

  for (int i=0; i<tracking_objects.size(); ++i) {
    std::string test_str;
    EXPECT_EQ(true, rt->tracks(tracking_objects[i]));
    rt->tracker_get(tracking_objects[i], test_str);
    EXPECT_EQ(test_str, tracking_objects[i]->str);
  }

  for (int i=0; i<tracking_objects.size(); ++i) {
    delete tracking_objects[i];
  }
  delete rt;
}


//*/


//////////////////////////////////////////////////////////////////////////////////

}
