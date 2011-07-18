//===-- AddressSpaceGraph.cpp -----------------------------------*- C++ -*-===//
//
// <insert license>
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//

#include "../Core/Context.h"
#include "AddressSpaceGraph.h"
#include "CVStream.h"

namespace cliver {

PointerEdge::PointerEdge() 
	: parent(NULL), 
		offset(0), 
		points_to_address(0), 
		points_to_node(NULL), 
		points_to_object(NULL), 
		next(NULL) {}

MemoryObjectNode::MemoryObjectNode(klee::ObjectState* obj) 
	: object_state(obj), 
		base_address(obj->getObject()->address)
		{}

PointerEdge* MemoryObjectNode::in_edge(unsigned i) { 
	assert(i < edges_in.size()); 
	return edges_in[i]; 
}

PointerEdge* MemoryObjectNode::out_edge(unsigned i) { 
	assert(i < edges_out.size()); 
	return edges_out[i];
}

unsigned MemoryObjectNode::in_degree() { 
	return edges_in.size();
}

unsigned MemoryObjectNode::out_degree() { 
	return edges_out.size();
}

void MemoryObjectNode::add_in_edge(PointerEdge *edge) {
	edges_in.push_back(edge);
}
void MemoryObjectNode::add_out_edge(PointerEdge *edge) {
	edges_out.push_back(edge);
}

AddressSpaceGraph::AddressSpaceGraph(klee::AddressSpace *address_space) 
 : address_space_(address_space), 
	 pointer_width_(klee::Context::get().getPointerWidth()) {
}

/// Build a graph on all the objects in the address space using pointer 
/// relationships as edges. 
void AddressSpaceGraph::build_graph() {
	// Create a MemoryObjectNode for each MemoryObject in the addressSpace.
	for (klee::MemoryMap::iterator it=address_space_->objects.begin(),
			ie=address_space_->objects.end(); it!=ie; ++it) {
		MemoryObjectNode *node = new MemoryObjectNode(it->second);
		nodes_.push_back(node);
		node_address_map_[node->address()] = node;
		node_object_map_[it->second] = node;
	}

	// The graph now has nodes but no edges. For each node create a list of PointerEdges
	// for each pointer contained in the MemoryObject that the node represents.
	for (unsigned i=0; i<nodes_.size(); ++i) {
		MemoryObjectNode *node = nodes_[i];
		extract_pointers(node);

		// Now for each PointerEdge, add it to the Node that it points to. Every node
		// should now have a complete list of incoming and outgoing edges (if they exist).
		for (unsigned j=0; j<node->out_degree(); ++j) {
			PointerEdge* edge = node->out_edge(j);
			MemoryObjectNode* points_to_node = node_object_map_[edge->points_to_object];
			assert(points_to_node != NULL && "Points-to-node is NULL");
			points_to_node->add_in_edge(edge);
		}
	}
}

int AddressSpaceGraph::compare(const AddressSpaceGraph &b) const {
	return 0;
}

/// Extract pointers using the ObjectState's pointerMask.
void AddressSpaceGraph::extract_pointers(MemoryObjectNode *node) {
	klee::ObjectState* obj = node->object();
	for (unsigned i=0; i<obj->size; ++i) {
		if (obj->isBytePointer(i)) {
			for (unsigned j=0; j<pointer_width_/8; ++j) {
				assert(obj->isBytePointer(i+j) && "invalid pointer size");
			}
			klee::ref<klee::Expr> pexpr = obj->read(i, pointer_width_);
			if (klee::ConstantExpr *CE = llvm::dyn_cast<klee::ConstantExpr>(pexpr)) {
				klee::ObjectPair object_pair;
			  uint64_t val = CE->getZExtValue(pointer_width_);
				if (address_space_->resolveOne(CE, object_pair)) {
					PointerEdge *pe = new PointerEdge();
					pe->parent = node;
					pe->offset = i;
					pe->points_to_address = val;
					pe->points_to_object = object_pair.second;
					node->add_out_edge(pe);
				} else {
					CVDEBUG("address " << *CE << " did not resolve");
				}
			} else {
				CVDEBUG("Non-concrete pointer");
			}
			i += (pointer_width_/8) - 1;
		}
	}
}

/// Extract pointers by trying to resolve every 'pointerwidth' constant expr
void AddressSpaceGraph::extract_pointers_by_resolving(MemoryObjectNode *node) {
	klee::ObjectState* obj = node->object();

	// Attempt to resolve every 4 or 8 byte constant expr in the ObjectState
	for (unsigned i=0; i<obj->size; ++i) {
		klee::ref<klee::Expr> pexpr = obj->read(i, pointer_width_);
		if (klee::ConstantExpr *CE = dyn_cast<klee::ConstantExpr>(pexpr)) {
			klee::ObjectPair object_pair;
			if (address_space_->resolveOne(CE, object_pair)) {
				PointerEdge *pe = new PointerEdge();
				pe->parent = node;
				pe->offset = i;
				pe->points_to_address = CE->getZExtValue(pointer_width_);
				pe->points_to_object = object_pair.second;
				node->add_out_edge(pe);
			}
		}
	}
}

} // end namespace cliver

