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

#include <map>
#include <boost/graph/adjacency_list.hpp>
#include <boost/graph/breadth_first_search.hpp>
#include <boost/graph/depth_first_search.hpp>

namespace cliver {

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
	boost::vecS, boost::vecS, boost::bidirectionalS,
	VertexProperties,
	PointerProperties > Graph;
typedef boost::graph_traits<Graph>::vertex_descriptor Vertex;
typedef boost::graph_traits<Graph>::vertex_iterator VertexIterator;
typedef std::pair< VertexIterator, VertexIterator > VertexPair;
typedef std::map< klee::ObjectState*, Vertex > ObjectVertexMap;
typedef std::pair< klee::ObjectState*, Vertex > ObjectVertexPair;

class BFSVisitor : public boost::default_bfs_visitor {
 public:
	BFSVisitor(std::set<Vertex> *_visited) : visited(_visited) {}
	template <typename Vertex, typename Graph>
	void discover_vertex(Vertex v, Graph& g) {
		if (visited->find(v) == visited->end()) {
			visited->insert(v);
		}
  }
	std::set<Vertex> *visited;
};

class AddressSpaceGraph {

 public:
	AddressSpaceGraph(klee::ExecutionState *state);
	void build_graph();
  int compare(const AddressSpaceGraph &b) const;
	void extract_pointers(klee::ObjectState *obj, PointerList &results);
	void extract_pointers_by_resolving(klee::ObjectState *obj, PointerList &results);
 private:
  bool compare_concrete(klee::ObjectState *a,klee::ObjectState *b);
	void add_vertex(klee::ObjectState* object);

	klee::ExecutionState *state_;
	unsigned pointer_width_;

	std::vector< klee::ObjectState* > stack_objects_;
	Graph graph_;
	ObjectVertexMap object_vertex_map_;
};

} // End cliver namespace

#endif
