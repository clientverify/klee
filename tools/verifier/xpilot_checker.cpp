#include "xpilot_checker.h"

#define DEBUG_TYPE "xpilot-checker"
#include "llvm/Support/Debug.h"

#include <stdio.h>
#include <dirent.h>
#include <unistd.h>
#include <algorithm>

#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>

using namespace klee;
using namespace llvm;

//===----------------------------------------------------------------------===//
// Class XPilotChecker

bool XPilotChecker::init() {
  bool success = false;
  if (input_constraints_dir_.size()) {
    prepareGenericConstraints(input_constraints_dir_, input_constraints_);
    success = true;
  }
  if (packet_constraints_dir_.size()) {
    prepareGenericConstraints(packet_constraints_dir_, packet_constraints_);
    success = true;
  }
  if (frames_dir_.size()) {
    prepareFrameLogs(frames_dir_);
  } else {
    success = false;
  }

  chain_complexity_limit_ = 64;
}

static void getFilesInDir(std::string dirname, std::vector<std::string> &files) {
  DIR *root = opendir(dirname.c_str());
  assert(root != NULL && "Error opening directory."); 

  struct dirent *dir;
  while((dir = readdir(root)) != 0) {
    std::string name = dir->d_name;
    if (name == "." || name == "..") continue;
    std::string fullname = dirname + "/" + name;
    if (dir->d_type == DT_DIR) {
      getFilesInDir(fullname, files);
    } else {
      files.push_back(fullname);
    }
  }
  closedir(root);
}

static bool XPilotFrame_compare(const XPilotFrame &a, const XPilotFrame &b) {
  return (a.id<b.id);
}

void XPilotChecker::getFrame(XPilotFrame &frame) {
  DIR *root = opendir(frame.path.c_str());
  assert(root != NULL && "Error opening directory."); 

  struct dirent *dir;
  while((dir = readdir(root)) != 0) {
    std::string name = dir->d_name;
    if (name == "." || name == "..") continue;

    if (dir->d_type != DT_DIR) {
      if (name.find("keyboard") == 0) {
        frame.input_log.push_back(name);
      } else if (name.find("packet") == 0) {
        frame.packet_log.push_back(name);
      }
    }
  }

  closedir(root);

  sort(frame.input_log.begin(), frame.input_log.end());
  sort(frame.packet_log.begin(), frame.packet_log.end());
}

void XPilotChecker::prepareGenericConstraints(
            std::string dirname, std::vector<GenericConstraint*> &constraints) {
  
  std::vector<std::string> filenames;
  DIR *root = opendir(dirname.c_str());
  assert(root != NULL && "Error opening directory."); 

  struct dirent *dir;
  while((dir = readdir(root)) != 0) {
    std::string name = dir->d_name;
    if (name == "." || name == "..") continue;

    if (dir->d_type != DT_DIR) {
      if (name.substr(name.size()-3) == ".pc" && name.substr(0,4) == "gsec") {
        filenames.push_back(dirname + "/" + name);
        //std::string fullpath = dirname + "/" + name; 
        //constraints.push_back(new GenericConstraint(fullpath));
      } 
    } 
  }
  closedir(root);

  sort(filenames.begin(), filenames.end());
  foreach (filename, filenames.begin(), filenames.end()) {
    constraints.push_back(new GenericConstraint(*filename));
  }

  DEBUG(llvm::cout << "Prepared " << constraints.size()
                   << " Constraints from: " << dirname << "\n");
}

void XPilotChecker::prepareFrameLogs(std::string framedir) {
  DIR *root = opendir(framedir.c_str());
  assert(root != NULL && "Error opening directory."); 

  struct dirent *dir;
  while((dir = readdir(root)) != 0) {
    std::string name = dir->d_name;
    if (name == "." || name == "..") continue;

    XPilotFrame frame;
    frame.name = name;
    frame.id = atoi(name.substr(6,6).c_str()); // format: frame_%06d
    frame.path = framedir + "/" + name;

    if (dir->d_type == DT_DIR) {
      getFrame(frame);
      frames_.push_back(frame);
    } 
  }
  closedir(root);
  sort(frames_.begin(), frames_.end(), XPilotFrame_compare);
  DEBUG(llvm::cout << "Prepared " << frames_.size()
    << " Frame Logs from: " << framedir << "\n");
}

bool XPilotChecker::checkValidity() {
  // load initial state
  if (frame_id_ == 0) {
    path_manager_.init(initial_state_file_);
  }

  while (frame_id_ < frames_.size()) {
    fstats = FrameStats();
    fstats.frame_number = frame_id_;
    XPilotFrame frame(nextFrame());

    std::deque< SegletContext > contexts;

    DEBUG(llvm::cout << "CHECKING FRAME (" << frame_id_ << ") \n");

    if (input_constraints_.size() && frame.input_log.size()) {
      foreach (file, frame.input_log.begin(), frame.input_log.end()) {
        std::string logfile = frame.path + "/" + *file;
        contexts.push_back(SegletContext(logfile, &input_constraints_));
      }
    }

    if (packet_constraints_.size() && frame.packet_log.size()) {
      foreach (file, frame.packet_log.begin(), frame.packet_log.end()) {
        std::string logfile = frame.path + "/" + *file;
        contexts.push_back(SegletContext(logfile, &packet_constraints_));
      }
    }

    fstats.input_loops = frame.input_log.size();
    fstats.packet_loops = frame.packet_log.size();

    int result = 0;

    do {
      result = checkContexts(contexts);
    } while (contexts.size() > 0 && result > 0);

    fstats.sat_acc_constraints = result;

    fstats.print();

    if (result > 0) {
      DEBUG(llvm::cout << "FRAME (" << frame_id_ 
              << ") has " << result << " chains.\n");
    } else {
      DEBUG(llvm::cout << "FRAME (" << frame_id_ << ") FAILED.\n");
      return false;
    }
  }
  return true;
}

int XPilotChecker::checkContexts(std::deque<SegletContext> &contexts) {
  PathSegletSetChain seglet_chain;

  while (contexts.size() > 0) {
    SegletContext context = contexts.front();
    contexts.pop_front();

    // Read state from log file.
    path_manager_.addStateData(context.logfile);

    // Collect common updates from PathSeglets in previous round.
    if (seglet_chain.size())
      path_manager_.current()->addCommonUpdates(seglet_chain.back());

    // Print current and previous StateData.
    if (path_manager_.previous()) {
      DEBUG(llvm::cout << "StateData: "<< path_manager_.previous()->id()
            << " (previous):\n" << *path_manager_.previous() << "\n");
    } else {
      DEBUG(llvm::cout << "Previous StateData: NULL \n");
    }
    DEBUG(llvm::cout << "StateData: "<< path_manager_.current()->id()
          << " (current):\n" << *path_manager_.current() << "\n");

    PathSegletSet seglets;

    foreach (GC, context.constraints->begin(), context.constraints->end()) {

      // Build new PathSeglet from generic constraint.
      PathSeglet seglet(path_manager_.previous(), path_manager_.current(), *GC);

      std::string file_id = context.logfile + " + " + (*GC)->filename();
      seglet.set_id(file_id);

      seglet.checkImpliedValues();

      if (seglet.valid()) {
        seglets.push_back(seglet);
        DEBUG(llvm::cout << "Valid PathSeglet: " << file_id << "\n");
        DEBUG(llvm::cout << "<BEGIN SEGLET>\n" << seglet << "\n<END SEGLET>\n");
      } else {
        //DEBUG(llvm::cout << "Invalid PathSeglet: " << file_id << "\n");
        //DEBUG(llvm::cout << "<BEGIN SEGLET>\n" << seglet << "\n<END SEGLET>\n");
      }
    }

    DEBUG(llvm::cout << "# of PathSeglets: " << seglets.size() << "\n");

    // Exit early if no path seglets found.
    if (seglets.size() == 0) return 0;

    seglet_chain.push_back(seglets);

    int chain_complexity = 1;
    foreach (SC, seglet_chain.begin(), seglet_chain.end())
      chain_complexity *= SC->size();

    if (chain_complexity > chain_complexity_limit_) {
      DEBUG(llvm::cout << "Performing Early Solver Query. "
        << "Chain Complexity(" << chain_complexity
        << ") > Limit(" << chain_complexity_limit_ << ").\n");
      break;
    }
  }

  path_manager_.extendChains(seglet_chain);
  
  int valid_chains = path_manager_.queryChains();
  return valid_chains;
  
}

void XPilotChecker::printFrameLogs() {
  foreach (frame, frames_.begin(), frames_.end()) {
    printf("Frame %d:\t\t%d keyboard log(s),\t%d packet log(s)\n", 
           frame->id, frame->input_log.size(), frame->packet_log.size());
  }
}

void XPilotChecker::printInputConstraints() {}

void XPilotChecker::printPacketConstraints() {}

//===----------------------------------------------------------------------===//
