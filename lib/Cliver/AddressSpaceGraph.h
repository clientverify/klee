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

#include <map>

namespace cliver {

class MemoryObjectNode;

struct PointerEdge {
	MemoryObjectNode *parent;
	unsigned offset;
	uint64_t points_to_address;
	MemoryObjectNode *points_to_node;
	const klee::ObjectState *points_to_object;
	PointerEdge *next;

	PointerEdge();
};

class MemoryObjectNode {
 public:
	klee::ObjectState* object_state;
	uint64_t base_address;

	unsigned in_degree;
	unsigned out_degree;
	PointerEdge *first_edge_in;
	PointerEdge *last_edge_in;
	PointerEdge *first_edge_out;
	PointerEdge *last_edge_out;

	MemoryObjectNode(klee::ObjectState* obj);
	void add_edge(PointerEdge *edge);
	void print();
};

class AddressSpaceGraph {
 public:
	AddressSpaceGraph(klee::AddressSpace *address_space);
	void build_graph();
  int compare(const AddressSpaceGraph &b) const;
	void extract_pointers(const klee::ObjectState &obj, MemoryObjectNode *node);
	void extract_pointers_by_resolving(const klee::ObjectState &obj, MemoryObjectNode *node);
 private:
	klee::AddressSpace *address_space_;
	unsigned pointer_width_;
	std::vector<MemoryObjectNode*> nodes_;
	std::map<uint64_t,MemoryObjectNode*> node_address_map_;
};

} // End cliver namespace

#endif
