//===-- ExecutionTree.cpp -====----------------------------------*- C++ -*-===//
//
// <insert license>
//
//===----------------------------------------------------------------------===//
// TODO: Method to merge ExecutionTrees
// TODO: Method to modify pre-existing ExecutionTree
// TODO: Method to split an ExecutionTree given a list of Leaf nodes
// TODO: Optimization: store BasicBlock entry id's in a vector rather than a 
//       path of nodes
// TODO: Unit tests for execution trees
// TODO: Remove static_casts in notify()
//===----------------------------------------------------------------------===//

#include "CVCommon.h"
#include "cliver/CVExecutor.h"
#include "cliver/EditDistance.h"
#include "cliver/ExecutionTree.h"
#include "cliver/CVExecutionState.h"
#include "klee/Internal/Module/KInstruction.h"
#include "klee/Internal/Module/KModule.h"
#include "klee/Internal/Module/InstructionInfoTable.h"
#include "llvm/Support/raw_ostream.h"

#include <boost/archive/binary_oarchive.hpp>
#include <boost/archive/binary_iarchive.hpp>
#include <boost/archive/text_oarchive.hpp>
#include <boost/archive/text_iarchive.hpp>

#include <fstream>

namespace cliver {

llvm::cl::opt<bool>
DebugExecutionTree("debug-execution-tree",llvm::cl::init(false));

llvm::cl::list<std::string> TrainingPathFile("training-path-file",
	llvm::cl::ZeroOrMore,
	llvm::cl::ValueRequired,
	llvm::cl::desc("Specify a training path file (.tpath)"),
	llvm::cl::value_desc("tpath directory"));

llvm::cl::list<std::string> TrainingPathDir("training-path-dir",
	llvm::cl::ZeroOrMore,
	llvm::cl::ValueRequired,
	llvm::cl::desc("Specify directory containint .tpath files"),
	llvm::cl::value_desc("tpath directory"));

#ifndef NDEBUG

#undef CVDEBUG
#define CVDEBUG(x) \
	__CVDEBUG(DebugExecutionTree, x);

#undef CVDEBUG_S
#define CVDEBUG_S(__state_id, __x) \
	__CVDEBUG_S(DebugExecutionTree, __state_id, __x)

#else

#undef CVDEBUG
#define CVDEBUG(x)

#undef CVDEBUG_S
#define CVDEBUG_S(__state_id, __x)

#endif

////////////////////////////////////////////////////////////////////////////////

// Helper for debug output
inline std::ostream &operator<<(std::ostream &os, 
		const klee::KInstruction &ki) {
	std::string str;
	llvm::raw_string_ostream ros(str);
	ros << ki.info->id << ":" << *ki.inst;
	//str.erase(std::remove(str.begin(), str.end(), '\n'), str.end());
	return os << ros.str();
}

////////////////////////////////////////////////////////////////////////////////

bool ExecutionTrace::operator==(const ExecutionTrace& b) const { 
  return basic_blocks_ == b.basic_blocks_;
  //if (size() != b.size())
  //  return false;

  //const_iterator it1 = begin(), it2 = b.begin();
  //const_iterator ie1 = end(), ie2 = b.end();

  //for (; it1!=ie1 && it2!=ie2; ++it1, ++it2) {
  //  if ((*it1)->id != (*it2)->id) {
  //    assert(((*it1)->bb != (*it2)->bb));
  //    return false;
  //  }
  //}

}
bool ExecutionTrace::operator!=(const ExecutionTrace& b) const { 
  return basic_blocks_ != b.basic_blocks_;
}

bool ExecutionTrace::operator<(const ExecutionTrace& b) const { 
  return basic_blocks_ < b.basic_blocks_;
}

void ExecutionTrace::push_back(const ExecutionTrace& etrace){
  basic_blocks_.insert(basic_blocks_.end(), etrace.begin(), etrace.end());
}

void ExecutionTrace::push_back_bb(const klee::KBasicBlock* kbb){
  basic_blocks_.push_back(kbb);
}

// XXX Inefficient on a vector!
void ExecutionTrace::push_front(const ExecutionTrace& etrace){
  basic_blocks_.insert(basic_blocks_.begin(), etrace.begin(), etrace.end());
}

// XXX Inefficient on a vector!
void ExecutionTrace::push_front_bb(const klee::KBasicBlock* kbb){
  basic_blocks_.insert(basic_blocks_.begin(), kbb);
}

void ExecutionTrace::deserialize(klee::KModule* km) {
  assert(serialized_basic_blocks_);
  foreach (unsigned bb_id, *serialized_basic_blocks_) {
    basic_blocks_.push_back(km->kbasicblocks[bb_id]);
  }
  delete serialized_basic_blocks_;
  serialized_basic_blocks_ = NULL;
}

void ExecutionTrace::write(std::ostream &os) {
	//boost::archive::binary_oarchive oa(os);
	boost::archive::text_oarchive oa(os);
  oa << *this;
}

void ExecutionTrace::read(std::ifstream &is, klee::KModule* kmodule) {
	//boost::archive::binary_iarchive ia(is);
	boost::archive::text_iarchive ia(is);
  ia >> *this;
  deserialize(kmodule);
}

std::ostream& operator<<(std::ostream& os, const ExecutionTrace &etrace) {
  foreach (const klee::KBasicBlock* kbb, etrace) {
    os << kbb->id << ", ";
  }
  return os;
}

////////////////////////////////////////////////////////////////////////////////

ExecutionTreeManager::ExecutionTreeManager(ClientVerifier* cv) : cv_(cv) {}

void ExecutionTreeManager::initialize() {
  trees_.push_back(new ExecutionTraceTree() );
}

void ExecutionTreeManager::notify(ExecutionEvent ev) {
  CVExecutionState* state = ev.state;
  CVExecutionState* parent = ev.parent;

  switch (ev.event_type) {
    case CV_ROUND_START: {
      trees_.push_back(new ExecutionTraceTree() );
      break;
    }
    case CV_BASICBLOCK_ENTRY: {
      if (!trees_.back()->has_state(state)) {
        CVDEBUG("Adding parent-less state: " << state << ", " << state->id() );
        trees_.back()->add_state(state, NULL);
      }
    
      ExecutionTrace etrace(state->prevPC->kbb);
      trees_.back()->update_state(state, etrace);
      break;
    }

    case CV_STATE_REMOVED: {
      CVDEBUG("Removing state: " << state << ", " << state->id() );
      trees_.back()->remove_state(state);
      break;
    }

    case CV_STATE_CLONE: {
      CVDEBUG("Cloned state: " << state << " : " << state->id() 
              << ", parent: " << parent << " : " << parent->id());

      trees_.back()->add_state(state, parent);
      break;
    }

    case CV_SOCKET_SHUTDOWN: {
      CVDEBUG("Successful socket shutdown. " << state << ":" << state->id());
      ExecutionTraceTree* tree = NULL;
      reverse_foreach (tree, trees_) {
        if (tree->has_state(state))
          break;
      }
      assert(tree->has_state(state));

      ExecutionTrace etrace;
      tree->get_path(state, etrace);
      CVMESSAGE("TRACE: " << etrace);
      break;
    }

    default:
      break;
  }
}

////////////////////////////////////////////////////////////////////////////////

TrainingExecutionTreeManager::TrainingExecutionTreeManager(ClientVerifier* cv) 
  : ExecutionTreeManager(cv) {}

void TrainingExecutionTreeManager::initialize() {
  trees_.push_back(new ExecutionTraceTree() );

}

void TrainingExecutionTreeManager::notify(ExecutionEvent ev) {
  CVExecutionState* state = ev.state;
  CVExecutionState* parent = ev.parent;

  switch (ev.event_type) {
    case CV_ROUND_START: {
      delete trees_.back();
      trees_.pop_back();
      trees_.push_back(new ExecutionTraceTree() );
      break;
    }
    case CV_BASICBLOCK_ENTRY: {

      if (!trees_.back()->has_state(state)) {
        CVDEBUG("Adding parent-less state: " << state << ", " << state->id() );
        trees_.back()->add_state(state, NULL);
      }

      ExecutionTrace etrace(state->prevPC->kbb);
      trees_.back()->update_state(state, etrace);

      break;
    }

    case CV_STATE_REMOVED: {
      CVDEBUG("Removing state: " << state << ", " << state->id() );
      trees_.back()->remove_state(state);
      break;
    }

    case CV_STATE_CLONE: {
      CVDEBUG("Cloned state: " << state << " : " << state->id() 
              << ", parent: " << parent << " : " << parent->id());
      trees_.back()->add_state(state, parent);
      break;
    }

    case CV_SOCKET_WRITE:
    case CV_SOCKET_READ: {
      assert(trees_.back()->has_state(state));
      ExecutionTrace etrace;
      trees_.back()->get_path(state, etrace);

      std::stringstream filename;
      filename << "state_" << state->id() 
        << "-round_" << cv_->round()
        << "-length_" << etrace.size()
        << ".tpath";
      std::ostream *file = cv_->openOutputFile(filename.str());
      etrace.write(*file);
      static_cast<std::ofstream*>(file)->close();
      
      break;
    }

    case CV_SOCKET_SHUTDOWN: {

      CVDEBUG("Successful socket shutdown. " << state << ":" << state->id());
      break;
    }

    default:
      break;
  }
}

////////////////////////////////////////////////////////////////////////////////

TrainingTestExecutionTreeManager::TrainingTestExecutionTreeManager(ClientVerifier* cv) 
  : ExecutionTreeManager(cv) {}

void TrainingTestExecutionTreeManager::initialize() {
  trees_.push_back(new ExecutionTraceTree() );

  // Read training paths
  if (!TrainingPathDir.empty()) {
    foreach(std::string path, TrainingPathDir) {
      cv_->cvstream()->getFiles(path, ".tpath", TrainingPathFile);
    }
  }
  if (TrainingPathFile.empty() || read_traces(TrainingPathFile) == 0) {
    cv_error("Error reading training path files, exiting now.");
  } 

  ed_tree_ = new EDTree();

  for (std::map<ExecutionTrace,std::string>::iterator it = training_trace_map_.begin(),
       ie = training_trace_map_.end(); it!=ie; ++it) {
    ed_tree_->insert(it->first, &(it->second));
  }
  std::vector<ExecutionTrace> trace_list;
  std::vector<const std::string*> name_list;
  ed_tree_->get_all_sequences(trace_list, &name_list);
  CVMESSAGE("Loaded " << trace_list.size() << " out of " 
          << training_trace_map_.size() << " training traces into the tree!");
  for (int i=0; i < trace_list.size(); ++i) {
    if (training_trace_map_.count(trace_list[i]) == 0) {
      CVMESSAGE("Trace missing for " << *(name_list[i]));
      cv_error("Error in EditDistanceTree, exiting.");
    }
  }
}

int TrainingTestExecutionTreeManager::read_traces(
    std::vector<std::string> &filename_list) {

  int dup_count = 0;
	foreach (std::string filename, filename_list) {

		std::ifstream *is = new std::ifstream(filename.c_str(),
				std::ifstream::in | std::ifstream::binary );

		if (is != NULL && is->good()) {
      ExecutionTrace etrace;
      etrace.read(*is, cv_->executor()->get_kmodule());

      if (training_trace_map_.count(etrace) == 0)
        training_trace_map_[etrace] = filename;
      else
        ++dup_count;

			delete is;
		}
	}
  CVMESSAGE("Duplicate traces " << dup_count );
	return training_trace_map_.size();
}

void TrainingTestExecutionTreeManager::notify(ExecutionEvent ev) {
  CVExecutionState* state = ev.state;
  CVExecutionState* parent = ev.parent;

  switch (ev.event_type) {
    case CV_ROUND_START: {
      trees_.push_back(new ExecutionTraceTree() );
      break;
    }
    case CV_BASICBLOCK_ENTRY: {

      if (!trees_.back()->has_state(state)) {
        CVDEBUG("Adding parent-less state: " << state << ", " << state->id() );
        trees_.back()->add_state(state, NULL);
      }

      ExecutionTrace etrace(state->prevPC->kbb);
      trees_.back()->update_state(state, etrace);

      break;
    }

    case CV_STATE_REMOVED: {
      CVDEBUG("Removing state: " << state << ", " << state->id() );
      trees_.back()->remove_state(state);
      break;
    }

    case CV_STATE_CLONE: {
      CVDEBUG("Cloned state: " << state << " : " << state->id() 
              << ", parent: " << parent << " : " << parent->id());
      trees_.back()->add_state(state, parent);
      break;
    }

    case CV_SOCKET_WRITE:
    case CV_SOCKET_READ: {
      static int count = 0;
      count++;
      if (count <= 1) break;
      assert(trees_.back()->has_state(state));
      ExecutionTrace etrace;
      trees_.back()->get_path(state, etrace);

      if (training_trace_map_.count(etrace)) {
        CVMESSAGE("Matching Training Trace Found! " << training_trace_map_[etrace]);
      } else {
        CVMESSAGE("Matching Training Trace Not Found!");
      }

      typedef EditDistanceTable<Score<ExecutionTrace,int>,ExecutionTrace,int> ExecutionTraceEDT;
      typedef EditDistanceRow<Score<ExecutionTrace,int>,ExecutionTrace,int> ExecutionTraceEDR;
      typedef EditDistanceUkkonen<Score<ExecutionTrace,int>,ExecutionTrace,int> ExecutionTraceEDU;
      typedef EditDistanceUkkonen<Score<ExecutionTrace,int>,ExecutionTrace,int> ExecutionTraceEDU;
      typedef EditDistanceDynamicUKK<Score<ExecutionTrace,int>,ExecutionTrace,int> ExecutionTraceEDUD;
      typedef EditDistanceStaticUKK<Score<ExecutionTrace,int>,ExecutionTrace,int> ExecutionTraceEDUS;
      typedef EditDistanceFullUKK<Score<ExecutionTrace,int>,ExecutionTrace,int> ExecutionTraceEDUF;

      for (std::map<ExecutionTrace,std::string>::iterator it = training_trace_map_.begin(),
           ie = training_trace_map_.end(); it!=ie; ++it) {
    
        int cost_u, cost_r;
        if (std::abs((int)etrace.size() - (int)(it->first).size()) > 500) {
          //ExecutionTraceEDR edr(etrace, it->first);
          //cost_r = edr.compute_editdistance();
          //CVMESSAGE("Cost: " << cost_r << " for edit distance row-table of " << it->second);
        } else {
          ExecutionTraceEDUF edu(etrace, it->first);
          cost_u = edu.compute_editdistance();
          //ExecutionTraceEDR edr(etrace, it->first);
          //cost_r = edr.compute_editdistance();
          //if (cost_u != cost_r)
          //  CVMESSAGE("*** cost_u = " << cost_u << ", cost_r = " << cost_r << " ***\n");
          //assert(cost_u == cost_r);
          CVMESSAGE("Cost: " << cost_u << " for edit distance full ukkonen of " << it->second);
 
        }
 
      }

      break;
    }

    case CV_SOCKET_SHUTDOWN: {

      CVDEBUG("Successful socket shutdown. " << state << ":" << state->id());
      break;
    }

    default:
      break;
  }
}

////////////////////////////////////////////////////////////////////////////////

VerifyExecutionTreeManager::VerifyExecutionTreeManager(ClientVerifier* cv) 
  : ExecutionTreeManager(cv), last_state_seen_(0) {}

void VerifyExecutionTreeManager::initialize() {
  trees_.push_back(new ExecutionTraceTree() );

  // Read training paths
  if (!TrainingPathDir.empty()) {
    foreach(std::string path, TrainingPathDir) {
      cv_->cvstream()->getFiles(path, ".tpath", TrainingPathFile);
    }
  }
  if (TrainingPathFile.empty() || read_traces(TrainingPathFile) == 0) {
    cv_error("Error reading training path files, exiting now.");
  } 
}

int VerifyExecutionTreeManager::read_traces(
    std::vector<std::string> &filename_list) {

  int dup_count = 0;
	foreach (std::string filename, filename_list) {

		std::ifstream *is = new std::ifstream(filename.c_str(),
				std::ifstream::in | std::ifstream::binary );

		if (is != NULL && is->good()) {
      ExecutionTrace etrace;
      etrace.read(*is, cv_->executor()->get_kmodule());

      if (training_trace_map_.count(etrace) == 0)
        training_trace_map_[etrace] = filename;
      else
        ++dup_count;

			delete is;
		}
	}
  CVMESSAGE("Duplicate traces " << dup_count );
	return training_trace_map_.size();
}

typedef EditDistanceRow<Score<ExecutionTrace,int>,ExecutionTrace,int> 
ExecutionTraceED;

void VerifyExecutionTreeManager::notify(ExecutionEvent ev) {
  CVExecutionState* state = ev.state;
  CVExecutionState* parent = ev.parent;

  switch (ev.event_type) {
    case CV_ROUND_START: {
      trees_.push_back(new ExecutionTraceTree() );
      break;
    }
    case CV_BASICBLOCK_ENTRY: {

      if (!trees_.back()->has_state(state)) {
        CVDEBUG("Adding parent-less state: " << state << ", " << state->id() );
        trees_.back()->add_state(state, NULL);
      }

      ExecutionTrace etrace(state->prevPC->kbb);
      trees_.back()->update_state(state, etrace);

      EditDistanceProperty *edp 
        = static_cast<EditDistanceProperty*>(state->property());
      //if (state != last_state_seen_) {
      if (edp->recompute) {
        edp->recompute = false;
        //last_state_seen_ = state;
        ExecutionTrace full_etrace;
        trees_.back()->get_path(state, full_etrace);

        CVDEBUG("Recalculating min edit_distance, trace_size: "
                << full_etrace.size() << ", prev min edit distance: " 
                << edp->edit_distance);

        std::map<ExecutionTrace,std::string>::iterator it 
          = training_trace_map_.begin(), ie = training_trace_map_.end(); 

        int min_edit_distance = INT_MAX;
        std::string min_edit_distance_str;
        for (; it!=ie; ++it) {
          if (it->first.size() > full_etrace.size()) {
            ExecutionTraceED ed(full_etrace, it->first);
            int edit_distance = ed.compute_editdistance();
            if (edit_distance < min_edit_distance) {
              min_edit_distance = edit_distance;
              min_edit_distance_str = it->second;
            }
            min_edit_distance = std::min(min_edit_distance, ed.compute_editdistance());
          }
        }
        edp->edit_distance = min_edit_distance;
        CVDEBUG("Min edit-distance: " << min_edit_distance
                << " " << min_edit_distance_str);
      }
      break;
    }

    case CV_STATE_REMOVED: {
      CVDEBUG("Removing state: " << state << ", " << state->id() );
      trees_.back()->remove_state(state);
      break;
    }

    case CV_STATE_CLONE: {
      CVDEBUG("Cloned state: " << state << " : " << state->id() 
              << ", parent: " << parent << " : " << parent->id());
      trees_.back()->add_state(state, parent);
      last_state_seen_ = state;
      
      EditDistanceProperty *edp 
        = static_cast<EditDistanceProperty*>(state->property());
      EditDistanceProperty *edp_parent
        = static_cast<EditDistanceProperty*>(parent->property());
      edp->recompute=true;
      edp_parent->recompute=true;

      break;
    }

    case CV_SOCKET_WRITE:
    case CV_SOCKET_READ: {

      ExecutionTrace full_etrace;
      trees_.back()->get_path(state, full_etrace);

      CVDEBUG("End of round, path length: " << full_etrace.size());

      break;
    }

    case CV_SOCKET_SHUTDOWN: {
      CVDEBUG("Successful socket shutdown. " << state << ":" << state->id());
      break;
    }

    default:
      break;
  }
}



} // end namespace cliver
