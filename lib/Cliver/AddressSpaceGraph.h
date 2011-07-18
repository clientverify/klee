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
class AddressSpaceGraph;

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
	MemoryObjectNode(klee::ObjectState* obj);
	klee::ObjectState* object() { return object_state; }
	uint64_t address() { return base_address; }
	void print();

	void add_out_edge(PointerEdge *edge);
	void add_in_edge(PointerEdge *edge);

	unsigned in_degree();
	unsigned out_degree();

	PointerEdge* in_edge(unsigned i);
	PointerEdge* out_edge(unsigned i);

 private:
	std::vector<PointerEdge*> edges_in;
	std::vector<PointerEdge*> edges_out;
	klee::ObjectState* object_state;
	uint64_t base_address;
};

class AddressSpaceGraph {
 public:
	AddressSpaceGraph(klee::AddressSpace *address_space);
	void build_graph();
  int compare(const AddressSpaceGraph &b) const;
	void extract_pointers(MemoryObjectNode *node);
	void extract_pointers_by_resolving(MemoryObjectNode *node);
 private:
	void add_edge_to_node(PointerEdge* edge, MemoryObjectNode* node);

	klee::AddressSpace *address_space_;
	unsigned pointer_width_;
	std::vector<MemoryObjectNode*> nodes_;
	std::map<uint64_t,MemoryObjectNode*> node_address_map_;
	std::map<const klee::ObjectState*,MemoryObjectNode*> node_object_map_;
};

} // End cliver namespace

#endif
