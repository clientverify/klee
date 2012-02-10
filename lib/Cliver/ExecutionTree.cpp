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

// XXX Inefficient
void ExecutionTrace::push_front(const ExecutionTrace& etrace){
  basic_blocks_.insert(basic_blocks_.begin(), etrace.begin(), etrace.end());
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
}

std::ostream& operator<<(std::ostream& os, const ExecutionTrace &etrace) {
  foreach (ExecutionTrace::BasicBlockID kbb, etrace) {
    os << kbb << ", ";
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
    
      trees_.back()->update_state(state, state->prevPC->kbb->id);
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

      trees_.back()->update_state(state, state->prevPC->kbb->id);
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
    CVMESSAGE("Adding " << it->second << ", size: " << (it->first).size()
              << " to the tree");
    ed_tree_->insert(it->first, &(it->second));
  }

  ed_tree_->initialize();

  std::vector<ExecutionTrace> trace_list;
  std::vector<const std::string*> name_list;
  ed_tree_->get_all_sequences(trace_list, &name_list);
  CVMESSAGE("Loaded " << trace_list.size() << " out of " 
          << training_trace_map_.size() << " training traces into the tree!");
  for (int i=0; i < trace_list.size(); ++i) {
    if (training_trace_map_.count(trace_list[i]) == 0) {
      CVMESSAGE("Trace missing for " << *(name_list[i]) 
                << ", size: " << trace_list[i].size()
                << " " << trace_list[i][0] << " "
                << trace_list[i][trace_list[i].size()-1]);
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
      delete trees_.back();
      // Delete previous round tree?
      trees_.push_back(new ExecutionTraceTree() );
      break;
    }
    case CV_BASICBLOCK_ENTRY: {

      if (!trees_.back()->has_state(state)) {
        CVDEBUG("Adding parent-less state: " << state << ", " << state->id() );
        trees_.back()->add_state(state, NULL);
      }

      trees_.back()->update_state(state, state->prevPC->kbb->id);
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

      // XXX REMOVE ME XXX
      if (cv_->round() == 5)
        cv_->executor()->setHaltExecution(true);
      // XXX REMOVE ME XXX

      ed_tree_->compute_t(etrace);
      std::vector<int> edit_distance_list;
      std::vector<const std::string*> name_list;
      std::map<const std::string*, int> name_map;
      ed_tree_->get_all_distances(edit_distance_list, &name_list);

      for (int i=0; i < edit_distance_list.size(); ++i) {
        name_map[name_list[i]] = edit_distance_list[i];
      }

      for (std::map<ExecutionTrace,std::string>::iterator it = training_trace_map_.begin(),
           ie = training_trace_map_.end(); it!=ie; ++it) {
    
        int cost_tree = name_map[&(it->second)];
        //if ((it->first).size() < 100) {
        int cost_u, cost_r;

        ExecutionTraceED edr(etrace, it->first);
        cost_r = edr.compute_editdistance();
        assert(name_map.count(&(it->second)));
        cost_tree = name_map[&(it->second)];

        cost_r = cost_tree;
        if (cost_tree != cost_r) {
          //*cv_message_stream << "________________________\n";
          //std::vector<std::vector<int> > cost_matrix;
          //std::vector< EDColumn > node_list;
          //ed_tree_->get_cost_matrix(&(it->second), cost_matrix);
          //ed_tree_->get_node_list(&(it->second), node_list);
          //for (int i=0; i<cost_matrix.size(); ++i) {
          //  *cv_message_stream << cost_matrix[i].size() << ", ";

          //}
          //*cv_message_stream << "\n------------------------\n";
          //for (int i=0; i<etrace.size()+1; ++i) {
          //  //for (int j=0; j<(it->first).size()+1; ++j) {
          //  for (int j=0; j<cost_matrix.size(); ++j) {
          //    int fwidth = cv_message_stream->width(4);
          //    *cv_message_stream << cost_matrix[j][i];
          //    cv_message_stream->width(fwidth);
          //  }
          //  *cv_message_stream << "\n";
          //}
          //*cv_message_stream << "\n========================\n";
          //for (int j=0; j<node_list.size(); ++j) {
          //  int fwidth = cv_message_stream->width(16);
          //  *cv_message_stream << node_list[j].s()[0];
          //  cv_message_stream->width(fwidth);
          //}
          //*cv_message_stream << "\n========================\n";

          ////for (int i=0; i<etrace.size()+1; ++i) {
          ////  for (int j=0; j<node_list.size(); ++j) {
          ////    int fwidth = cv_message_stream->width(4);
          ////    *cv_message_stream << cost_matrix[j][i];
          ////    cv_message_stream->width(fwidth);
          ////  }
          ////  *cv_message_stream << "\n";
          ////}

          //edr.debug_print(*cv_message_stream);
          CVMESSAGE("*** cost_tree = " << cost_tree << ", cost_r = " 
                    << cost_r << " *** for " << it->second);
          cv_error("exiting");
        } else {
          CVMESSAGE("EditDist (row, tree):  " << cost_tree << ", " << cost_r
                    << " for " << it->second);
        }



        //if (std::abs((int)etrace.size() - (int)(it->first).size()) > 500) {
        //  //ExecutionTraceEDR edr(etrace, it->first);
        //  //cost_r = edr.compute_editdistance();
        //  //CVMESSAGE("Cost: " << cost_r << " for edit distance row-table of " << it->second);
        //} else {
        //  ExecutionTraceEDUF edu(etrace, it->first);
        //  cost_u = edu.compute_editdistance();
        //  //ExecutionTraceEDR edr(etrace, it->first);
        //  //cost_r = edr.compute_editdistance();
        //  //if (cost_u != cost_r)
        //  //  CVMESSAGE("*** cost_u = " << cost_u << ", cost_r = " << cost_r << " ***\n");
        //  //assert(cost_u == cost_r);
        //  CVMESSAGE("Cost: " << cost_u << " for edit distance full ukkonen of " << it->second);
 
        //}
 
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

      trees_.back()->update_state(state, state->prevPC->kbb->id);

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
