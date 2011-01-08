#ifndef XPILOT_CHECKER_H
#define XPILOT_CHECKER_H

#include "path_segment.h"

#include <deque>

namespace klee {
//===----------------------------------------------------------------------===//

struct XPilotFrame {
  std::vector<std::string> input_log;
  std::vector<std::string> packet_log;
  int id;
  std::string name;
  std::string path;
};

class SegletContext {
public:
  SegletContext(std::string _logfile,
                std::vector<GenericConstraint*> *_constraints)
    : logfile(_logfile), constraints(_constraints) {}

  std::string name;
  std::string logfile;
  std::vector<GenericConstraint*> *constraints;
};

class XPilotChecker {
public:

  XPilotChecker(std::string inputdir, std::string packetdir, 
                std::string framesdir, std::string initialstate)
    : input_constraints_dir_(inputdir), 
      packet_constraints_dir_(packetdir),
      frame_id_(0),
      frames_dir_(framesdir),
      initial_state_file_(initialstate) {}

  bool init();
  void prepareGenericConstraints(std::string inputdir, 
                                 std::vector<GenericConstraint*> &constraints);
  void prepareFrameLogs(std::string framedir);
  void printFrameLogs();
  void printInputConstraints();
  void printPacketConstraints();
  const XPilotFrame& nextFrame() { return frames_[frame_id_++]; }
  int checkContexts(std::deque<SegletContext> &contexts);
  bool checkValidity();

private:
  void getFrame(XPilotFrame &frame);
  std::string input_constraints_dir_; 
  std::string packet_constraints_dir_;
  std::vector<GenericConstraint*> input_constraints_;
  std::vector<GenericConstraint*> packet_constraints_;

  std::vector<GenericConstraint*> runtime_constraints_;

  unsigned frame_id_;
  std::string frames_dir_;
  std::vector< XPilotFrame > frames_;

  std::string initial_state_file_;

  PathManager path_manager_;
  unsigned chain_complexity_limit_;

};

} // end namespace klee

#endif // XPILOT_CHECKER_H
