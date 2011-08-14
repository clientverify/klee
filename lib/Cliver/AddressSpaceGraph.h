//===-- AddressSpaceGraph.h -------------------------------------*- C++ -*-===//
//
// <insert license>
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//
#ifndef CLIVER_ADDRESSSPACEGRAPH_H
#define CLIVER_ADDRESSSPACEGRAPH_H

#include "../Core/AddressSpace.h"
#include "../Core/Memory.h"
#include "klee/ExecutionState.h"
#include "klee/util/ExprVisitor.h"
#include "klee/IndependentElementSet.h"
#include "CVStream.h"

#include <map>
#include <boost/graph/adjacency_list.hpp>
#include <boost/graph/breadth_first_search.hpp>
#include <boost/graph/depth_first_search.hpp>

namespace cliver {

class CVExecutionState;

typedef std::pair<klee::ObjectState&,klee::ObjectState&> ObjectStatePair;
typedef std::map<const klee::MemoryObject*, klee::ObjectState*> MemoryObjectMap;

struct VertexProperties {
	klee::ObjectState *object;
};

struct PointerProperties {
	unsigned offset; // the offset location of this pointer
	uint64_t address; // the address the pointer points to (i.e., the pointer value)
	klee::ObjectState *object; // the object the pointer points to

};
typedef std::vector< PointerProperties > PointerList;

typedef boost::adjacency_list<
	boost::vecS, 
	boost::vecS, 
	boost::bidirectionalS,
	VertexProperties,
	PointerProperties > Graph;

typedef boost::graph_traits<Graph>::vertex_descriptor Vertex;
typedef boost::graph_traits<Graph>::vertex_iterator VertexIterator;
typedef boost::graph_traits<Graph>::out_edge_iterator OutEdgeIterator;
typedef std::pair< VertexIterator, VertexIterator > VertexPair;
typedef std::map< klee::ObjectState*, Vertex > ObjectVertexMap;
typedef std::pair< klee::ObjectState*, Vertex > ObjectVertexPair;

class AddressSpaceGraph {
friend class AddressSpaceGraphVisitor;

 public:
	AddressSpaceGraph(klee::ExecutionState *state);
	void build();
	void process();
  bool equal(const AddressSpaceGraph &b) const;
	bool equal(const AddressSpaceGraph &b, 
			std::set<klee::ObjectState*> &non_equal_concretes) const;

	void extract_pointers(klee::ObjectState *obj, PointerList &results);
	void extract_pointers_by_resolving(klee::ObjectState *obj, PointerList &results);

	std::set<const klee::Array*> &arrays() { return arrays_; }
	std::vector<const klee::Array*> &in_order_arrays() { return in_order_arrays_; }
	klee::ref<klee::Expr> get_canonical_expr(const AddressSpaceGraph &b,
			klee::ref<klee::Expr> e) const;

 private:

	// Check equivalence of ObjectStates in the context of the AddressSpaceGraph
  bool objects_equal(klee::ObjectState &a,klee::ObjectState &b) const;
	bool objects_equal(const AddressSpaceGraph &asg_b, klee::ObjectState &a, 
			klee::ObjectState &b) const;
	bool objects_equal(const AddressSpaceGraph &asg_b, klee::ObjectState &a, 
			klee::ObjectState &b, bool &candidate_symbolic_merge) const;

	// Helpers for checking the equivalence of AddressSpaceGraphs
	bool array_size_equal(const AddressSpaceGraph &b) const;
	bool locals_equal(const AddressSpaceGraph &b) const;
	bool local_objects_equal(const AddressSpaceGraph &b) const;
	bool visited_size_equal(const AddressSpaceGraph &b) const;
	bool unconnected_objects_equal(const AddressSpaceGraph &b) const;
	bool connected_objects_equal(const AddressSpaceGraph &b) const;
	bool graphs_equal(const AddressSpaceGraph &b) const;


	void add_vertex(klee::ObjectState* object);
	void add_arrays_from_expr(klee::ref<klee::Expr> e);

	// Member variables
	klee::ExecutionState *state_;
	CVExecutionState *cv_state_;
	unsigned pointer_width_;

	Graph graph_;

	ObjectVertexMap object_vertex_map_;
	ObjectVertexMap unconnected_;
	std::vector<Vertex> in_order_visited_;

	std::set<klee::ObjectState*> root_objects_;

	std::set<const klee::Array*> arrays_;
	std::vector< std::pair<klee::ref<klee::Expr>, klee::ObjectState*> > locals_;
	std::map<const klee::Array*, unsigned> array_map_;
	std::vector<const klee::Array*> in_order_arrays_;

  MemoryObjectMap unconnected_map_;
};

class AddressSpaceGraphVisitor: public boost::default_bfs_visitor {
 private:
	AddressSpaceGraph &asg_;
	std::set<Vertex> &visited_;

 public:
	AddressSpaceGraphVisitor(AddressSpaceGraph &asg, std::set<Vertex> &visited)
		: asg_(asg), visited_(visited) {}

	template <typename Vertex, typename Graph>
	void discover_vertex(Vertex v, Graph& g) {

		if (visited_.find(v) == visited_.end()) {

			// Extract the object state from the vertex properties
			klee::ObjectState* object_state 
				= boost::get(boost::get(&VertexProperties::object, asg_.graph_), v);

			// Extract any references to reads of other symbolic variables
			for (unsigned i=0; i<object_state->size; ++i) {
				if(!object_state->isByteConcrete(i)) {
					klee::ref<klee::Expr> expr = object_state->read8(i);
					asg_.add_arrays_from_expr(expr);
				}
			}

			visited_.insert(v);
			asg_.in_order_visited_.push_back(v);
		}
  }
};

class ReplaceArrayVisitor : public klee::ExprVisitor {
private:
	std::map<const klee::Array*, unsigned> array_map_;
	std::vector<const klee::Array*> arrays_;

public:
  ReplaceArrayVisitor(std::map<const klee::Array*, unsigned> array_map,
			std::vector<const klee::Array*> arrays) 
		: klee::ExprVisitor(true), array_map_(array_map), arrays_(arrays) {
		assert(array_map_.size() == arrays_.size());
	}

  Action visitRead(const klee::ReadExpr &e) {
		if (e.updates.root != NULL) {
      if (array_map_.count(e.updates.root) == 0) {
				return Action::doChildren();
			}
			unsigned to_replace_index = array_map_[e.updates.root];
			const klee::Array *array = arrays_[to_replace_index];

			// Because extend() pushes a new UpdateNode onto the list, we need to walk
			// the list in reverse to rebuild it in the same order.
			std::vector< const klee::UpdateNode*> update_list;
			for (const klee::UpdateNode *un=e.updates.head; un; un=un->next) {
				update_list.push_back(un);
			}

			// walk list in reverse
			klee::UpdateList updates(array, NULL);
			reverse_foreach (const klee::UpdateNode* U, update_list) {
				updates.extend(visit(U->index), visit(U->value));
			}

			return Action::changeTo(klee::ReadExpr::create(updates, visit(e.index)));

		}
		return Action::doChildren();
  }
};


} // End cliver namespace

#endif
