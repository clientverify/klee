//===-- SocketEvent.cpp -----------------------------------------*- C++ -*-===//
//
//===----------------------------------------------------------------------===//

//#include "packet.h"

#include "gtest/gtest.h"

#include "cliver/Socket.h"
#include "cliver/ClientVerifier.h" /* for ClientModelFlag */
#include "cliver/SocketEventMeasurement.h"

#include "klee/Internal/ADT/KTest.h"

#include <vector>

//////////////////////////////////////////////////////////////////////////////////
// Under test: SocketEventMeasurement
//////////////////////////////////////////////////////////////////////////////////

using namespace cliver;

// Helper to return end of array
template<typename T, size_t N> T * end(T (&ra)[N]) { return ra + N; }

//////////////////////////////////////////////////////////////////////////////////

class SocketEventMeasurementTest : public ::testing::Test {
 protected:

  virtual void SetUp() {
    ClientModelFlag = Tetrinet;
    tetrinet_ktest_ = kTest_fromFile("tetrinet.ktest");
    ASSERT_TRUE(tetrinet_ktest_ != NULL);
    ASSERT_EQ(tetrinet_ktest_->numObjects, 13);

    tetrinet_socket_events_ = new SocketEventList();
    for (unsigned i=0; i<tetrinet_ktest_->numObjects; ++i) {
      tetrinet_socket_events_->push_back(
        new SocketEvent(tetrinet_ktest_->objects[i]));
    }

    ClientModelFlag = XPilot;
    xpilot_ktest_ = kTest_fromFile("xpilot1.ktest");
    ASSERT_TRUE(xpilot_ktest_ != NULL);
    //ASSERT_EQ(xpilot_ktest_->numObjects, 266);

    xpilot_socket_events_ = new SocketEventList();
    for (unsigned i=0; i<xpilot_ktest_->numObjects; ++i) {
      xpilot_socket_events_->push_back(
        new SocketEvent(xpilot_ktest_->objects[i]));
    }
  }

  virtual void TearDown() {
    delete tetrinet_ktest_;
    delete xpilot_ktest_;
  }

  //static void SetUpTestCase() {}
  
  // Tetrinet test data
  KTest *tetrinet_ktest_;
  SocketEventList *tetrinet_socket_events_;

  // XPilot test data
  KTest *xpilot_ktest_;
  SocketEventList *xpilot_socket_events_;
};

//////////////////////////////////////////////////////////////////////////////////

namespace {

TEST_F(SocketEventMeasurementTest, Ktest) {
  KTest *ktest = kTest_fromFile("tetrinet.ktest");
  ASSERT_TRUE(ktest != NULL);
  ASSERT_EQ(ktest->numObjects, 13);
  delete ktest;
}

TEST_F(SocketEventMeasurementTest, XPilot) {
  SocketEventSimilarityXpilot measure;
  for (unsigned i=0; i<xpilot_socket_events_->size(); ++i) {
    //std::cout << *((*xpilot_socket_events_)[i]) << std::endl;
    for (unsigned j=0; j<xpilot_socket_events_->size(); ++j) {
      //std::cout << *((*xpilot_socket_events_)[i]) << std::endl;
      //std::cout << *((*xpilot_socket_events_)[j]) << std::endl;
      double score = measure.similarity_score(
          (*xpilot_socket_events_)[i],
          (*xpilot_socket_events_)[j]);
      if (i == j) {
        EXPECT_EQ(score, 0.0f);
      } else {
        EXPECT_LE(score, 1.0f);
        EXPECT_GT(score, 0.0f);
      }
      ////std::cout << "score: " << score << std::endl;
    }
  }
}


TEST_F(SocketEventMeasurementTest, Tetrinet) {
  SocketEventSimilarityTetrinet measure;
  for (unsigned i=0; i<tetrinet_socket_events_->size(); ++i) {
    for (unsigned j=0; j<tetrinet_socket_events_->size(); ++j) {
      //std::cout << *((*tetrinet_socket_events_)[i]) << std::endl;
      //std::cout << *((*tetrinet_socket_events_)[j]) << std::endl;
      double score = measure.similarity_score(
          (*tetrinet_socket_events_)[i],
          (*tetrinet_socket_events_)[j]);
      if (i == j) {
        EXPECT_EQ(score, 0.0f);
      } else {
        EXPECT_LE(score, 1.0f);
        EXPECT_GT(score, 0.0f);
      }
      //std::cout << "score: " << score << std::endl;
    }
  }
}

//TEST_F(SocketEventMeasurementTest, Init) {
//  SocketEventSimilarityTetrinet measure;
//  EXPECT_LT(0.0f, measure.similarity_score(socket_events_[0],socket_events_[1]));
//  EXPECT_LT(0.0f, measure.similarity_score(socket_events_[1],socket_events_[0]));
//  EXPECT_EQ(1.0f, measure.similarity_score(socket_events_[0],socket_events_[2]));
//  EXPECT_EQ(0.0f, measure.similarity_score(socket_events_[2],socket_events_[3]));
//  for (int i=0; i<socket_events_.size(); i++) {
//    EXPECT_EQ(0.0f, measure.similarity_score(socket_events_[i],socket_events_[i]));
//  }
//}

//////////////////////////////////////////////////////////////////////////////////
}


