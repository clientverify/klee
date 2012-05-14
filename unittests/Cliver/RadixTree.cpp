//===-- RadixTree.cpp -------------------------------------------*- C++ -*-===//
//
// <insert license>
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//

#include "gtest/gtest.h"

#include "cliver/RadixTree.h"
#include "cliver/TrackingRadixTree.h"
#include "cliver/LevenshteinRadixTree.h"
#include "cliver/EditDistance.h"
#include "cliver/KExtensionTree.h"
#include "RadixTree.inc"

#include <stdlib.h>
#include <string>
#include <iostream>
#include <set>

using namespace cliver;

//////////////////////////////////////////////////////////////////////////////////

// Helper to return end of array
template<typename T, size_t N> T * end(T (&ra)[N]) { return ra + N; }

template<typename T>
std::ostream& operator<<(std::ostream &os, const std::vector<T>& v) {
  for (typename std::vector<T>::const_iterator it=v.begin(), ie=v.end(); it!=ie; ++it)
    os << *it;
  return os;
}

template<typename T>
std::ostream& operator<<(std::ostream &os, const std::list<T>& v) {
  for(typename std::list<T>::const_iterator it=v.begin(), ie=v.end(); it!=ie; ++it)
    os << *it;
  return os;
}
//////////////////////////////////////////////////////////////////////////////////

// Static vars
std::vector<std::string> dictionary;

//////////////////////////////////////////////////////////////////////////////////
// Under test: RadixTree and RadixTree inheriting classes 
//////////////////////////////////////////////////////////////////////////////////

// Tested in RadixTreeTest
typedef RadixTree<std::string, char>             StringRadixTree;
typedef RadixTree<std::vector<char>, char>       VectorRadixTree;
typedef RadixTree<std::list<char>, char>         ListRadixTree;

// Tested in TrackingRadixTreeTest
struct TrackingObject { unsigned id; };

typedef TrackingRadixTree<std::string, char, TrackingObject>       StringTrackingRadixTree;
typedef TrackingRadixTree<std::vector<char>, char, TrackingObject> VectorTrackingRadixTree;
typedef TrackingRadixTree<std::list<char>, char, TrackingObject>   ListTrackingRadixTree;

//typedef EdgeTrackingRadixTree<std::string, char, TrackingObject>       StringEdgeTrackingRadixTree;
//typedef EdgeTrackingRadixTree<std::vector<char>, char, TrackingObject> VectorEdgeTrackingRadixTree;
//typedef EdgeTrackingRadixTree<std::list<char>, char, TrackingObject>   ListEdgeTrackingRadixTree;


// Tested in EditDistanceTreeTest
typedef LevenshteinRadixTree<std::string, char>  StringLevenshteinRadixTree;
typedef KLevenshteinRadixTree<std::string, char> StringKLevenshteinRadixTree;
typedef KExtensionOptTree<std::string, char>     StringKExtensionOptTree;
typedef KExtensionTree<std::string, char>        StringKExtensionTree;

//////////////////////////////////////////////////////////////////////////////////

using ::testing::Types;

//typedef Types<> Implementations;
typedef Types<
//  StringRadixTree, 
//  VectorRadixTree, 
//  ListRadixTree
> Implementations;


//typedef Types<> TrackingImplementations;
typedef Types<
//  StringTrackingRadixTree, 
//  VectorTrackingRadixTree, 
//  ListTrackingRadixTree
> TrackingImplementations;

//typedef Types<> EditDistanceImplementations;
typedef Types<
//  StringLevenshteinRadixTree,
//  StringKLevenshteinRadixTree,
//  StringKExtensionOptTree,
//  StringKExtensionTree
> EditDistanceImplementations;

//////////////////////////////////////////////////////////////////////////////////
// EditDistance typedefs for verifiying LevenshteinRadixTree, this classes are
// not tested here

typedef Score<std::string, char, int> StringScore;
typedef EditDistanceTable<StringScore,std::string,int> StringEDT;
typedef EditDistanceRow<StringScore,std::string,int> StringEDR;

//////////////////////////////////////////////////////////////////////////////////

template<class RadixTreeType>
class RadixTreeTest : public ::testing::Test {
 protected:

  virtual void SetUp() {
    rt_ = new RadixTreeType();

    std::string s1, s2, s3, s4, s5, s6, s7, s8;

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

  void SetupDictionary() {
    if (dictionary.empty())
      dictionary = std::vector<std::string>(cstr_dictionary, end(cstr_dictionary));
    for (unsigned i=0; i<dictionary.size(); ++i) {
      typename RadixTreeType::sequence_type 
          v(dictionary[i].begin(), dictionary[i].end());
      v_dictionary.push_back(v);
    }
  }

  void InsertDictionary() {
    this->SetupDictionary();
    for (unsigned i=0; i<v_dictionary.size(); ++i) {
      rt_->insert(v_dictionary[i]);
    }
  }

  void InsertSmallWords() {
    rt_->insert(v_empty);
    rt_->insert(v1);
    rt_->insert(v2);
    rt_->insert(v3);
    rt_->insert(v4);
    rt_->insert(v5);
    rt_->insert(v6);
    rt_->insert(v7);
    rt_->insert(v8);
  }

  virtual void TearDown() {
    delete rt_;
  }

  RadixTreeType* rt_;
  std::vector<typename RadixTreeType::sequence_type> v_dictionary;
  typename RadixTreeType::sequence_type v_empty, v1, v2, v3, v4, v5, v6, v7, v8;
};

//////////////////////////////////////////////////////////////////////////////////

template<class RadixTreeType>
class TrackingRadixTreeTest : public ::testing::Test {
 protected:

  typedef typename RadixTreeType::sequence_type SequenceType;
  typedef std::vector<SequenceType> SequenceList;
  typedef std::vector<TrackingObject*> TrackingObjectList;
  typedef std::map<TrackingObject*, SequenceType> TrackingObjectMap;

  virtual void SetUp() {
    rt_ = new RadixTreeType();

    if (dictionary.empty())
      dictionary = std::vector<std::string>(cstr_dictionary, end(cstr_dictionary));

    for (unsigned i=0; i<dictionary.size(); ++i) {
      typename RadixTreeType::sequence_type 
          v(dictionary[i].begin(), dictionary[i].end());
      v_dictionary.push_back(v);
    }

    srand(0);
    for (unsigned i=0; i<v_dictionary.size(); ++i) {
      TrackingObject* tobj = new TrackingObject();
      tobj->id = i;
      list_.push_back(tobj);
    }
  }

  void ExtendDictionary() {
    for (unsigned i=0; i<v_dictionary.size(); ++i) {
      rt_->extend(v_dictionary[i], list_[i]);
    }
  }

  void RandomExtend(RadixTreeType* rt, int count) {
  
    int r0, r1, r2;
    for (int i=0; i < count;  ++i) {
      r0 = rand() % this->v_dictionary.size();
      r1 = rand() % this->v_dictionary.size();
      r2 = rand() % this->v_dictionary.size();
      if (this->list_[r0] != NULL) {
        TrackingObject* tobj     = this->list_[r0];
        TrackingObject* tobj_new = new TrackingObject();
        this->list_.push_back(tobj_new);
        tobj_new->id = this->list_.size();

        EXPECT_EQ(rt->clone_tracker(tobj_new, tobj), true);

        SequenceType seq_ext_1, seq_ext_2;
        seq_ext_1.insert(seq_ext_1.end(), 
                        this->v_dictionary[r1].begin(),
                        this->v_dictionary[r1].end());
        seq_ext_1.insert(seq_ext_1.end(), '_');

        rt->extend(seq_ext_1, tobj);

        seq_ext_1.insert(seq_ext_1.begin(), 
                        this->v_dictionary[r0].begin(),
                        this->v_dictionary[r0].end());

        seq_ext_2.insert(seq_ext_2.begin(), 
                        this->v_dictionary[r2].begin(),
                        this->v_dictionary[r2].end());

        seq_ext_2.insert(seq_ext_2.end(), '_');

        rt->extend(seq_ext_2, tobj_new);

        seq_ext_2.insert(seq_ext_2.begin(), 
                        this->v_dictionary[r0].begin(),
                        this->v_dictionary[r0].end());

        this->v_dictionary[r0] = seq_ext_1;
        this->v_dictionary.push_back(seq_ext_2);
      }
    }
  }

  virtual void TearDown() {
    delete rt_;

    for (unsigned i=0; i<list_.size(); ++i) {
      if (list_[i])
        delete list_[i];
    }
  }

  RadixTreeType* rt_;
  SequenceList v_dictionary;
  TrackingObjectList list_;
};

//////////////////////////////////////////////////////////////////////////////////

typedef KLevenshteinRadixTree<std::string, char> StringKLevRadixTree;

template<class EditDistanceTreeType>
class EditDistanceTreeTest : public ::testing::Test {
 protected:

  virtual void SetUp() {
    rt_ = new EditDistanceTreeType();
  }

  void SetupDictionary() {
    if (dictionary.empty())
      dictionary = std::vector<std::string>(cstr_dictionary, end(cstr_dictionary));
    for (unsigned i=0; i<dictionary.size(); ++i) {
      typename EditDistanceTreeType::sequence_type 
          v(dictionary[i].begin(), dictionary[i].end());
      v_dictionary.push_back(v);
    }
  }

  void InsertDictionary() {
    this->SetupDictionary();
    for (unsigned i=0; i<v_dictionary.size(); ++i) {
      rt_->insert(v_dictionary[i]);
    }
  }

  virtual void TearDown() {
    delete rt_;
  }

  EditDistanceTreeType* rt_;
  std::vector<typename EditDistanceTreeType::sequence_type> v_dictionary;
};

//////////////////////////////////////////////////////////////////////////////////

TYPED_TEST_CASE(RadixTreeTest, Implementations);
TYPED_TEST_CASE(TrackingRadixTreeTest, TrackingImplementations);
//TYPED_TEST_CASE(EditDistanceTreeTest, EditDistanceImplementations);

//////////////////////////////////////////////////////////////////////////////////

namespace {

//////////////////////////////////////////////////////////////////////////////////

//TYPED_TEST(EditDistanceTreeTest, Init) {
//  ASSERT_TRUE(this->rt_ != NULL);
//}
//
//TYPED_TEST(EditDistanceTreeTest, Insert) {
//  ASSERT_TRUE(this->rt_ != NULL);
//  this->InsertDictionary();
//
//  for (unsigned i = 0; i<this->v_dictionary.size(); ++i) {
//    EXPECT_EQ(this->rt_->lookup(this->v_dictionary[i]), true);
//  }
//}
//
//TYPED_TEST(EditDistanceTreeTest, Clone) {
//  ASSERT_TRUE(this->rt_ != NULL);
//  this->InsertDictionary();
//
//  TypeParam* clone_rt = static_cast<TypeParam*>(this->rt_->clone());
//  delete this->rt_;
//  this->rt_ = clone_rt;
//
//  for (unsigned i = 0; i<this->v_dictionary.size(); ++i) {
//    EXPECT_EQ(clone_rt->lookup(this->v_dictionary[i]), true);
//  }
//}

//////////////////////////////////////////////////////////////////////////////////

TYPED_TEST(RadixTreeTest, Init) {
  ASSERT_TRUE(this->rt_ != NULL);
}

TYPED_TEST(RadixTreeTest, Insert) {
  this->InsertSmallWords();
  ASSERT_TRUE(this->rt_->element_count() == 22);
}

TYPED_TEST(RadixTreeTest, Lookup) {
  EXPECT_EQ(this->rt_->lookup(this->v1), false);

  this->rt_->insert(this->v1);
  EXPECT_EQ(this->rt_->lookup(this->v2), false);

  this->rt_->insert(this->v2);
  EXPECT_EQ(this->rt_->lookup(this->v1), true);
  EXPECT_EQ(this->rt_->lookup(this->v2), true);

  this->rt_->insert(this->v3);
  EXPECT_EQ(this->rt_->lookup(this->v3), true);
  EXPECT_EQ(this->rt_->lookup(this->v4), true);
  EXPECT_EQ(this->rt_->lookup(this->v5), false);
  EXPECT_EQ(this->rt_->lookup(this->v7), false);
}

TYPED_TEST(RadixTreeTest, Remove) {
  this->InsertSmallWords();
  EXPECT_EQ(this->rt_->lookup(this->v1), true);
  EXPECT_EQ(this->rt_->remove(this->v2), true);
  EXPECT_EQ(this->rt_->lookup(this->v2), false);
  EXPECT_EQ(this->rt_->remove(this->v8), false);
  EXPECT_EQ(this->rt_->lookup(this->v8), true);
  EXPECT_EQ(this->rt_->lookup(this->v6), true);
  EXPECT_EQ(this->rt_->remove(this->v6), true);
  EXPECT_EQ(this->rt_->lookup(this->v6), false);
}

TYPED_TEST(RadixTreeTest, Clone) {
  this->InsertSmallWords();

  TypeParam *clone_rt = this->rt_->clone();
  delete this->rt_;
  EXPECT_EQ(clone_rt->lookup(this->v1), true);
  EXPECT_EQ(clone_rt->remove(this->v2), true);
  EXPECT_EQ(clone_rt->lookup(this->v2), false);
  EXPECT_EQ(clone_rt->remove(this->v8), false);
  EXPECT_EQ(clone_rt->lookup(this->v8), true);
  EXPECT_EQ(clone_rt->lookup(this->v6), true);
  EXPECT_EQ(clone_rt->remove(this->v6), true);
  EXPECT_EQ(clone_rt->lookup(this->v6), false);
  this->rt_ = clone_rt;
}

TYPED_TEST(RadixTreeTest, InsertDictionary) {
  this->InsertDictionary();
  ASSERT_TRUE(this->rt_->element_count() == 389308);
}

TYPED_TEST(RadixTreeTest, LookupDictionary) {
  this->InsertDictionary();
  for (unsigned i = 0; i<this->v_dictionary.size(); ++i) {
    EXPECT_EQ(this->rt_->lookup(this->v_dictionary[i]), true);
  }
}

TYPED_TEST(RadixTreeTest, RemoveDictionary) {
  this->InsertDictionary();
  
  srand(0);
  unsigned r = 0, freq = 10;
  while (r < this->v_dictionary.size()) {
    bool result = this->rt_->remove(this->v_dictionary[r]);
    EXPECT_EQ(this->rt_->lookup(this->v_dictionary[r]), !result);
    r += 1 + (rand() % freq);
  }
}

TYPED_TEST(RadixTreeTest, CloneDictionary) {
  this->InsertDictionary();

  TypeParam* clone_rt = this->rt_->clone();
  delete this->rt_;

  srand(0);
  unsigned r = 0, freq = 10;
  while (r < this->v_dictionary.size()) {
    bool result = clone_rt->remove(this->v_dictionary[r]);
    EXPECT_EQ(clone_rt->lookup(this->v_dictionary[r]), !result);
    r += 1 + (rand() % freq);
  }
  this->rt_ = clone_rt;
}

//////////////////////////////////////////////////////////////////////////////////

TYPED_TEST(TrackingRadixTreeTest, Init) {
  ASSERT_TRUE(this->rt_ != NULL);
}

TYPED_TEST(TrackingRadixTreeTest, Extend) {
  TypeParam *rt = this->rt_;
  this->ExtendDictionary();

  for (unsigned i=0; i<this->list_.size(); ++i) {
    typename TypeParam::sequence_type seq;
    EXPECT_EQ(true, rt->tracks(this->list_[i]));
    rt->tracker_get(this->list_[i], seq);
    EXPECT_EQ(seq, this->v_dictionary[i]);
  }
}

// This is a test of a unoptimized pattern of access where each TrackingObject
// increases in size by one element only each extension and TrackingObjects
// share prefixes as they increase in size.
TYPED_TEST(TrackingRadixTreeTest, ExtendElement) {
  TypeParam *rt = this->rt_;

  int count = 2000; //s_dictionary.size();
  for (int i=0; i<count; ++i) {
    typename TypeParam::sequence_type::iterator it = this->v_dictionary[i].begin();
    typename TypeParam::sequence_type::iterator ie = this->v_dictionary[i].end();
    for (; it != ie; ++it) {
      rt->extend_element(*it, this->list_[i]);
    }
  }

  for (unsigned i=0; i<count; ++i) {
    typename TypeParam::sequence_type seq;
    EXPECT_EQ(true, rt->tracks(this->list_[i]));
    rt->tracker_get(this->list_[i], seq);
    EXPECT_EQ(seq, this->v_dictionary[i]);
  }
}

TYPED_TEST(TrackingRadixTreeTest, ExtendAndClone) {
  TypeParam *rt = this->rt_;

  this->ExtendDictionary();

  srand(0);
  int count = 5;

  this->RandomExtend(rt, count);

  for (unsigned i=0; i<this->list_.size(); ++i) {
    typename TypeParam::sequence_type seq;
    EXPECT_EQ(true, rt->tracks(this->list_[i]));
    rt->tracker_get(this->list_[i], seq);
    EXPECT_EQ(seq, this->v_dictionary[i]);
  }
}

TYPED_TEST(TrackingRadixTreeTest, ExtendAndRemove) {
  TypeParam *rt = this->rt_;

  this->ExtendDictionary();

  srand(0);
  int r0, count = 5;

  for (int i=0; i < count;  ++i) {
    r0 = rand() % this->v_dictionary.size();
    if (this->list_[i] != NULL) {
      rt->remove_tracker(this->list_[i]);
      delete this->list_[i];
      this->list_[i] = NULL;
    }
  }
 
  this->RandomExtend(rt, count);

  for (unsigned i=0; i<this->list_.size(); ++i) {
    if (this->list_[i] != NULL) {
      typename TypeParam::sequence_type seq;
      EXPECT_EQ(true, rt->tracks(this->list_[i]));
      rt->tracker_get(this->list_[i], seq);
      EXPECT_EQ(seq, this->v_dictionary[i]);
    }
  }
}

TYPED_TEST(TrackingRadixTreeTest, ExtendAndRemoveWithClone) {
  TypeParam *rt = this->rt_;

  this->ExtendDictionary();

  srand(0);
  int r0, count = 5;

  for (int i=0; i < count;  ++i) {
    r0 = rand() % this->v_dictionary.size();
    if (this->list_[i] != NULL) {
      rt->remove_tracker(this->list_[i]);
      delete this->list_[i];
      this->list_[i] = NULL;
    }
  }

  rt = static_cast<TypeParam*>(rt->clone());
  delete this->rt_;
  this->rt_ = rt;

  this->RandomExtend(rt, count);

  for (unsigned i=0; i<this->list_.size(); ++i) {
    if (this->list_[i] != NULL) {
      typename TypeParam::sequence_type seq;
      EXPECT_EQ(true, rt->tracks(this->list_[i]));
      rt->tracker_get(this->list_[i], seq);
      EXPECT_EQ(seq, this->v_dictionary[i]);
    }
  }
}

//////////////////////////////////////////////////////////////////////////////////

}
