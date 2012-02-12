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
#include <algorithm>

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

std::ostream& operator<<(std::ostream& os, const ExecutionTraceInfo &info) {
  os << "(trace id:" << info.id << ") "
     << "(length:" << info.trace->size() << ") "
     << "(" << info.name << ") ";
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
      CVDEBUG("TRACE: " << etrace);
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

  foreach(ExecutionTraceInfo* info, execution_traces_) {
    CVMESSAGE("Adding " << *info );
    ed_tree_->insert(*(info->trace), info->id);
  }

  // Initialize the tree
  ed_tree_->initialize();

  // Retrieve ExecutionTraces from the recently created tree.
  std::vector<ExecutionTrace> trace_list;
  std::vector<ExecutionTrace::ID> id_list;
  ed_tree_->get_all_sequences(trace_list, &id_list);

  CVMESSAGE("Loaded " << trace_list.size() << " out of " 
            << execution_traces_.size() << " traces into tree");

  for (int i=0; i < trace_list.size(); ++i) {
    if (execution_trace_set_.count(&(trace_list[i]))) {
      CVMESSAGE("Trace missing: " << id_list[i]);
      cv_error("Error in EditDistanceTree, exiting.");
    }
  }

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
      assert(trees_.back()->has_state(state));

      ExecutionTrace etrace;
      trees_.back()->get_path(state, etrace);

      if (execution_trace_set_.count(&etrace)) {
        CVMESSAGE("Matching Training Trace Found! ");
      } else {
        CVMESSAGE("Matching Training Trace Not Found!");
      }

      CVMESSAGE("cloning edit distance tree");
      EDTree* ed_tree = ed_tree_->clone();
      
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

  et_by_length_ = ExecutionTraceInfoList(execution_traces_.begin(), 
                                         execution_traces_.end());
  ExecutionTraceInfoLengthLT length_comp;
  std::sort(et_by_length_.begin(), 
            et_by_length_.end(), length_comp);


  ed_tree_ = new EDTree();

  CVMESSAGE("Adding training paths...");
  //foreach(ExecutionTraceInfo* info, execution_traces_) {
  foreach(ExecutionTraceInfo* info, et_by_length_) {
    CVMESSAGE("Adding " << *info << " to the tree");
    ed_tree_->insert(*(info->trace), info->id);
  }
  CVMESSAGE("Done.");

  // Initialize the tree
  ed_tree_->initialize();
}

int VerifyExecutionTreeManager::min_edit_distance() {
  while (removed_states_.count(current_min_.top().first))
    current_min_.pop();

  if (current_min_.empty())
    return INT_MAX;

  return current_min_.top().second;
}

void VerifyExecutionTreeManager::update_min_edit_distance(
    CVExecutionState* state, int ed) {
  if (current_min_.empty() || ed < current_min_.top().second) {
    while (!current_min_.empty() && current_min_.top().first == state)
      current_min_.pop();
    current_min_.push(std::make_pair(state, ed));
  }
}

void VerifyExecutionTreeManager::reset_min_edit_distance() {
  CVDEBUG("Clearing min edit distance stack of size: " << current_min_.size());
  // XXX most efficient way to do this?
  while (!current_min_.empty())
    current_min_.pop();
}

int VerifyExecutionTreeManager::read_traces(
    std::vector<std::string> &filename_list) {

  static ExecutionTrace::ID starting_id=0;
  int dup_count = 0;
	foreach (std::string filename, filename_list) {

		std::ifstream *is = new std::ifstream(filename.c_str(),
				std::ifstream::in | std::ifstream::binary );

		if (is != NULL && is->good()) {
      ExecutionTrace* etrace = new ExecutionTrace();
      etrace->read(*is, cv_->executor()->get_kmodule());

      if (execution_trace_set_.count(etrace) == 0) {
        ExecutionTrace::ID eid = ++starting_id;
        ExecutionTraceInfo* info = new ExecutionTraceInfo();
        info->trace = etrace;
        info->id = eid;
        info->name = filename;

        execution_traces_.push_back(info);
        execution_trace_set_.insert(etrace);
        id_map_[eid] = info;

      } else {
        delete etrace;
        ++dup_count;
      }

			delete is;
		}
	}
  CVMESSAGE("Duplicate traces " << dup_count );
	
  return execution_trace_set_.size();
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
      reset_min_edit_distance();
      break;
    }
    case CV_BASICBLOCK_ENTRY: {

      EditDistanceProperty *edp 
        = static_cast<EditDistanceProperty*>(state->property());

      if (!trees_.back()->has_state(state)) {
        trees_.back()->add_state(state, NULL);
        EDTree* ed_tree = ed_tree_->clone();
        edp->edit_distance = INT_MAX;
        edp->recompute=true;
        state_tree_map_[state] = ed_tree;
        CVDEBUG("Adding parent-less state: " << *state );
      }

      trees_.back()->update_state(state, state->prevPC->kbb->id);
      
      if (edp->recompute) {
        edp->recompute = false;
        ExecutionTrace full_etrace;
        trees_.back()->get_path(state, full_etrace);

        CVDEBUG("Recalculating min edit_distance, trace_size: "
                << full_etrace.size() << "; state " << *state);

        int min_ed = INT_MAX;
        ExecutionTrace::ID trace_id;

        std::vector<ExecutionTrace::ID> search_list;
        int current_min_ed = min_edit_distance();


        int prefix_sz = full_etrace.size();

        // Select any X such that |X| <= max(|s|,|t|) - min(|s|,|t|)
        foreach (ExecutionTraceInfo* info, et_by_length_) {
          int training_sz = info->trace->size();
          if (current_min_ed > std::abs(prefix_sz - training_sz)) {
            if (full_etrace[0] == (*info->trace)[0]) {
              search_list.push_back(info->id);
            }
          }
        }

        if (search_list.empty()) {
          int min_diff = INT_MAX;
          ExecutionTrace::ID min_id;
          foreach (ExecutionTraceInfo* info, et_by_length_) {
            int diff = std::abs(prefix_sz - ((int)info->trace->size()));
            if (min_diff > diff) {
              if (full_etrace[0] == (*info->trace)[0]) {
                min_diff = diff;
                min_id = info->id;
              }
            }
          }
          search_list.push_back(min_id);
        }
        assert(!search_list.empty());

        CVDEBUG("Computing edit distance for " << search_list.size() 
                << " of " << et_by_length_.size() << " traces.");

        state_tree_map_[state]->compute_t(full_etrace, 
                                          current_min_ed, 
                                          search_list,
                                          &min_ed,
                                          &trace_id);

        edp->edit_distance = min_ed;
        CVDEBUG("Min edit-distance: " << min_ed << " " << *(id_map_[trace_id]));
        update_min_edit_distance(state, min_ed);
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
      CVDEBUG("Cloned state: " << *state << ", parent: " << *parent )
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
      EditDistanceProperty *edp 
        = static_cast<EditDistanceProperty*>(state->property());

      CVDEBUG("End of round, path length: " << full_etrace.size());
      CVDEBUG("End state: " << *state);

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
