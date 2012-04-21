//===-- SocketEvent.cpp ---------------------------------------------------===//
//
//===----------------------------------------------------------------------===//

#include "gtest/gtest.h"

#include "cliver/Socket.h"
#include "cliver/SocketEventMeasurement.h"

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
    char *se_data[] = {
      "p 1 4 4 0", 
      "p 3 2 9 1", 
      "newgame apple 2240352930", 
      "newgame orange", 

      "endgame "
    };

    str_data_ = std::vector<std::string>(se_data, end(se_data));

    for (int i=0; i < str_data_.size(); ++i) {
      const char* str = str_data_[i].c_str();
      socket_events_.push_back(new SocketEvent((const unsigned char*)str,
                                               str_data_[i].size()));
    }
  }

  virtual void TearDown() {}

  //static void SetUpTestCase() {}
  std::vector<SocketEvent*> socket_events_;
  std::vector<std::string> str_data_;
};

//////////////////////////////////////////////////////////////////////////////////

namespace {

TEST_F(SocketEventMeasurementTest, Init) {
  SocketEventSimilarityTetrinet measure;
  EXPECT_LT(0.0f, measure.similarity_score(socket_events_[0],socket_events_[1]));
  EXPECT_LT(0.0f, measure.similarity_score(socket_events_[1],socket_events_[0]));
  EXPECT_EQ(1.0f, measure.similarity_score(socket_events_[0],socket_events_[2]));
  EXPECT_EQ(0.0f, measure.similarity_score(socket_events_[2],socket_events_[3]));
  for (int i=0; i<socket_events_.size(); i++) {
    EXPECT_EQ(0.0f, measure.similarity_score(socket_events_[i],socket_events_[i]));
  }
}

//////////////////////////////////////////////////////////////////////////////////
}


