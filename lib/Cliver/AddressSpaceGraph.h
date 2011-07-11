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

namespace cliver {

class MemoryObjectNode;

struct PointerEdge {
	uint64_t address;
	uint64_t base_address;
	unsigned offset;
	uint64_t points_to_address;
	uint64_t points_to_base_address;
	unsigned points_to_offset;
	MemoryObjectNode *points_to_node;
	const klee::ObjectState *points_to_object;
	PointerEdge *next;

	PointerEdge();
};

class MemoryObjectNode {
 public:
	unsigned degree;
	uint64_t base_address;
	klee::ObjectState* object_state;
	PointerEdge *first_edge;
	PointerEdge *last_edge;

	MemoryObjectNode();
	void add_edge(PointerEdge *edge);
	void print();
};

class AddressSpaceGraph {
 public:
	AddressSpaceGraph(klee::AddressSpace *address_space);
	void build_graph();
  int compare(const AddressSpaceGraph &b) const;
	void test_extract_pointers();
 private:
	void extract_pointers(const klee::ObjectState &obj, MemoryObjectNode *node);
	void extract_pointers_by_resolving(const klee::ObjectState &obj, MemoryObjectNode *node);

	klee::AddressSpace *address_space_;
	unsigned pointer_width_;
	std::vector<MemoryObjectNode*> nodes_;
};

} // End cliver namespace

#endif
