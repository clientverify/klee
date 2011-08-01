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
#include "klee/IndependentElementSet.h"

#include <map>
#include <boost/graph/adjacency_list.hpp>
#include <boost/graph/breadth_first_search.hpp>
#include <boost/graph/depth_first_search.hpp>

namespace cliver {

class CVExecutionState;

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

class BFSVisitor : public boost::default_bfs_visitor {
 public:
	BFSVisitor(
			std::set<Vertex> *_visited, 
			std::vector<Vertex> *_in_order_visited,
			klee::IndependentElementSet *_arrays) 
		: visited(_visited), 
		  in_order_visited(_in_order_visited), 
		  arrays(_arrays) {}

	template <typename Vertex, typename Graph>
	void discover_vertex(Vertex v, Graph& g) {
		if (visited->find(v) == visited->end()) {
			// Extract the object state from the vertex properties
			klee::ObjectState* object_state 
				= boost::get(boost::get(&VertexProperties::object, g), v);

			// Extract any references to reads of other symbolic variables
			for (unsigned i=0; i<object_state->size; ++i) {
				if(!object_state->isByteConcrete(i)) {
					klee::ref<klee::Expr> read_expr = object_state->read8(i);
					arrays->add(klee::IndependentElementSet(read_expr));
				}
			}

			visited->insert(v);
			in_order_visited->push_back(v);
		}
  }

	std::set<Vertex> *visited;
	std::vector<Vertex> *in_order_visited;
	klee::IndependentElementSet *arrays;
};

class DFSVisitor : public boost::default_dfs_visitor {
 public:
	DFSVisitor(std::vector<ObjectVertexPair> *_in_order_discovered)
		: in_order_discovered(_in_order_discovered) {}

	template <typename Vertex, typename Graph>
	void discover_vertex(Vertex v, Graph& g) {
		if (discovered.find(v) == discovered.end()) {
			// Extract the object state from the vertex properties
			klee::ObjectState* object_state 
				= boost::get(boost::get(&VertexProperties::object, g), v);
			discovered.insert(v);
			in_order_discovered->push_back(ObjectVertexPair(object_state, v));
		}
  }

	std::vector<ObjectVertexPair> *in_order_discovered;
	std::set<Vertex> discovered;
};


class AddressSpaceGraph {

 public:
	AddressSpaceGraph(klee::ExecutionState *state);
	void build();
  //int compare(const AddressSpaceGraph &b) const;
  bool equals(const AddressSpaceGraph &b) const;
	void extract_pointers(klee::ObjectState *obj, PointerList &results);
	void extract_pointers_by_resolving(klee::ObjectState *obj, PointerList &results);
	klee::IndependentElementSet& arrays() { return arrays_; }

 private:
  bool concrete_compare(klee::ObjectState &a,klee::ObjectState &b) const;
	void add_vertex(klee::ObjectState* object);

	klee::ExecutionState *state_;
	CVExecutionState *cv_state_;
	unsigned pointer_width_;

	std::vector< klee::ObjectState* > stack_objects_;
	Graph graph_;
	ObjectVertexMap object_vertex_map_;
	ObjectVertexMap unconnected_objects_;
	ObjectVertexMap connected_objects_;
	klee::IndependentElementSet arrays_;
	std::vector<Vertex> in_order_visited_;
};

} // End cliver namespace

#endif
