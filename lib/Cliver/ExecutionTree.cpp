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

TestExecutionTreeManager::TestExecutionTreeManager(ClientVerifier* cv) 
  : VerifyExecutionTreeManager(cv) {}

void TestExecutionTreeManager::initialize() {
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

  for (ExecutionTraceIDMap::iterator it = training_trace_map_.begin(),
       ie = training_trace_map_.end(); it!=ie; ++it) {
    CVMESSAGE("Adding " << training_name_map_[it->second] 
              << ", size: " << (it->first).size() << " to the tree");
    ed_tree_->insert(it->first, it->second);
  }

  std::vector<ExecutionTrace> trace_list;
  std::vector<ExecutionTrace::ID> id_list;
  ed_tree_->get_all_sequences(trace_list, &id_list);
  CVMESSAGE("Loaded " << trace_list.size() << " out of " 
          << training_trace_map_.size() << " training traces into the tree!");
  for (int i=0; i < trace_list.size(); ++i) {
    if (training_trace_map_.count(trace_list[i]) == 0) {
      CVMESSAGE("Trace missing for " << training_name_map_[id_list[i]]
                << ", size: " << trace_list[i].size()
                << " " << trace_list[i][0] << " "
                << trace_list[i][trace_list[i].size()-1]);
      cv_error("Error in EditDistanceTree, exiting.");
    }
  }

  ed_tree_->initialize();
}

void TestExecutionTreeManager::notify(ExecutionEvent ev) {
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

      //// XXX REMOVE ME XXX
      //if (cv_->round() == 5)
      //  cv_->executor()->setHaltExecution(true);
      //// XXX REMOVE ME XXX

      CVMESSAGE("cloning edit distance tree");
      EDTree* ed_tree = ed_tree_->clone();
      //EDTree* ed_tree = new EDTree();
      //int tcount =0;
      //for (ExecutionTraceIDMap::iterator it = training_trace_map_.begin(),
      //    ie = training_trace_map_.end(); it!=ie; ++it) {
      //  ed_tree->insert(it->first, it->second);
      //  if (++tcount == 1)
      //    break;
      //}
      //ed_tree->initialize();
      //ed_tree->clone();
      
      CVMESSAGE("computing edit distance in tree");
      ed_tree->compute_t(etrace);
      //std::vector<int> edit_distance_list;
      //std::vector<ExecutionTrace::ID> id_list;
      //std::map<ExecutionTrace::ID, int> id_distance_map;
      //ed_tree->get_all_distances(edit_distance_list, &id_list);

      //for (int i=0; i < edit_distance_list.size(); ++i) {
      //  id_distance_map[id_list[i]] = edit_distance_list[i];
      //}

      //for (ExecutionTraceIDMap::iterator it = training_trace_map_.begin(),
      //     ie = training_trace_map_.end(); it!=ie; ++it) {
    
      //  int cost_u, cost_r=0;
      //  int cost_tree = id_distance_map[it->second];

      //  ExecutionTraceED edr(etrace, it->first);
      //  cost_r = edr.compute_editdistance();
      //  //assert(id_distance_map.count(it->second));

      //  //cost_r = cost_tree;
      //  if (cost_tree != cost_r) {
      //    CVMESSAGE("*** cost_tree = " << cost_tree << ", cost_r = " 
      //              << cost_r << " *** for " << training_name_map_[it->second]);
      //    //cv_error("exiting");
      //  } else {
      //    CVMESSAGE("EditDist (row, tree):  " << cost_tree << ", " << cost_r
      //              << " for " << training_name_map_[it->second]);
      //  }
 
      //}
      delete ed_tree;
      CVMESSAGE("DONE!");

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
  : ExecutionTreeManager(cv) {}

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

  ed_tree_ = new EDTree();

  CVMESSAGE("Adding training paths..");
  for (ExecutionTraceIDMap::iterator it = training_trace_map_.begin(),
       ie = training_trace_map_.end(); it!=ie; ++it) {
    //CVMESSAGE("Adding " << training_name_map_[it->second] 
    //          << ", size: " << (it->first).size() << " to the tree");
    ed_tree_->insert(it->first, it->second);
  }
  CVMESSAGE("Done.");
  ed_tree_->initialize();
}

int VerifyExecutionTreeManager::read_traces(
    std::vector<std::string> &filename_list) {

  static ExecutionTrace::ID starting_id=0;
  int dup_count = 0;
	foreach (std::string filename, filename_list) {

		std::ifstream *is = new std::ifstream(filename.c_str(),
				std::ifstream::in | std::ifstream::binary );

		if (is != NULL && is->good()) {
      ExecutionTrace etrace;
      etrace.read(*is, cv_->executor()->get_kmodule());

      if (training_trace_map_.count(etrace) == 0) {
        ExecutionTrace::ID eid = ++starting_id;

        training_trace_map_[etrace] = eid;
        training_name_map_[eid] = filename;
      } else {
        ++dup_count;
      }

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
      delete trees_.back();
      trees_.push_back(new ExecutionTraceTree() );
      ExecutionStateEDTreeMap::iterator it = state_tree_map_.begin(),
          ie = state_tree_map_.end();
      for (; it!=ie; ++it) {
        delete it->second;
      }
      state_tree_map_.clear();
      break;
    }
    case CV_BASICBLOCK_ENTRY: {

      if (!trees_.back()->has_state(state)) {
        CVDEBUG("Adding parent-less state: " << state << ", " << state->id() );
        trees_.back()->add_state(state, NULL);
        EDTree* ed_tree = ed_tree_->clone();
        state_tree_map_[state] = ed_tree;
      }

      trees_.back()->update_state(state, state->prevPC->kbb->id);

      EditDistanceProperty *edp 
        = static_cast<EditDistanceProperty*>(state->property());
      
      if (edp->recompute) {
        edp->recompute = false;
        ExecutionTrace full_etrace;
        trees_.back()->get_path(state, full_etrace);

        CVDEBUG("Recalculating min edit_distance, trace_size: "
                << full_etrace.size() << ", prev min edit distance: " 
                << edp->edit_distance);

        int min_ed = INT_MAX;
        ExecutionTrace::ID trace_id;
        state_tree_map_[state]->compute_t(full_etrace);
        state_tree_map_[state]->min_edit_distance(min_ed, trace_id);
        
        ////// Begin Testing 

        //std::vector<int> edit_distance_list;
        //std::vector<ExecutionTrace::ID> id_list;
        //// Collect Edit Distances from Tree
        //state_tree_map_[state]->get_all_distances(edit_distance_list, &id_list);

        //std::map<ExecutionTrace::ID, int> id_distance_map;
        //for (int i=0; i < edit_distance_list.size(); ++i) {
        //  id_distance_map[id_list[i]] = edit_distance_list[i];
        //  if (min_ed > edit_distance_list[i]) {
        //    min_ed = edit_distance_list[i];
        //    trace_id = id_list[i];
        //  }
        //}

        //for (ExecutionTraceIDMap::iterator it = training_trace_map_.begin(),
        //    ie = training_trace_map_.end(); it!=ie; ++it) {
      
        //  int cost_r;
        //  int cost_tree = id_distance_map[it->second];

        //  ExecutionTraceED edr(full_etrace, it->first);
        //  cost_r = edr.compute_editdistance();
        //  if (cost_tree != cost_r) {
        //    CVMESSAGE("*** cost_tree = " << cost_tree << ", cost_r = " 
        //              << cost_r << " *** for " << training_name_map_[it->second]);
        //    //cv_error("exiting");
        //  //} else {
        //  //  CVMESSAGE("EditDist (row, tree):  " << cost_tree << ", " << cost_r
        //  //            << " for " << training_name_map_[it->second]);
        //  }
        //}
  

        ////ExecutionTrace::ID min_classic_id;
        ////int min_classic_ed = INT_MAX;
        ////ExecutionTraceIDMap::iterator it = training_trace_map_.begin(), 
        ////  ie = training_trace_map_.end(); 
        ////for (; it!=ie; ++it) {
        ////  ExecutionTraceED ed(full_etrace, it->first);
        ////  int edit_distance = ed.compute_editdistance();
        ////  if (edit_distance < min_classic_ed) {
        ////    min_classic_ed = edit_distance;
        ////    min_classic_id= it->second;
        ////  }
        ////}

        ////if (min_classic_ed != min_ed || min_classic_id != trace_id)
        ////  CVMESSAGE("*** cost_tree = " << min_ed << ", cost_r = " 
        ////            << min_classic_ed << " *** for " << trace_id << " and " << min_classic_id);
        ////// End Testing

        edp->edit_distance = min_ed;
        CVDEBUG("Min edit-distance: " << min_ed
                << " " << training_name_map_[trace_id]);
      }
      break;
    }

    case CV_STATE_REMOVED: {
      CVDEBUG("Removing state: " << state << ", " << state->id() );
      trees_.back()->remove_state(state);
      assert(state_tree_map_.count(state));
      delete state_tree_map_[state];
      state_tree_map_.erase(state);
      break;
    }

    case CV_STATE_CLONE: {
      CVDEBUG("Cloned state: " << state << " : " << state->id() 
              << ", parent: " << parent << " : " << parent->id());
      trees_.back()->add_state(state, parent);
      
      EditDistanceProperty *edp 
        = static_cast<EditDistanceProperty*>(state->property());
      EditDistanceProperty *edp_parent
        = static_cast<EditDistanceProperty*>(parent->property());
      edp->recompute=true;
      edp_parent->recompute=true;

      // copy on write instead?
      assert(state_tree_map_.count(parent));
      EDTree* ed_tree = state_tree_map_[parent]->clone();
      state_tree_map_[state] = ed_tree;

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
