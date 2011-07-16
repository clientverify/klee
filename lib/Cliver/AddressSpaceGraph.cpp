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
	: degree(0), 
		object_state(obj), 
		base_address(obj->getObject()->address), 
		first_edge(NULL), 
		last_edge(NULL) {}

void MemoryObjectNode::add_edge(PointerEdge *edge) {
	if (degree == 0) {
		first_edge = last_edge = edge;
	} else {
		last_edge->next = edge;
		last_edge = edge;
	}
	degree++;
}

AddressSpaceGraph::AddressSpaceGraph(klee::AddressSpace *address_space) 
 : address_space_(address_space), 
	 pointer_width_(klee::Context::get().getPointerWidth()) {
}

/// Build a graph on all the objects in the address space using pointer 
/// relationships as edges. 
void AddressSpaceGraph::build_graph() {
	for (klee::MemoryMap::iterator it=address_space_->objects.begin(),
			ie=address_space_->objects.end(); it!=ie; ++it) {
		MemoryObjectNode *mon = new MemoryObjectNode(it->second);
		node_address_map_[mon->base_address] = mon;
		extract_pointers(*it->second, mon);
		nodes_.push_back(mon);
	}
	//for (::iterator it=address_space_->objects.begin(),
  //			ie=address_space_->objects.end(); it!=ie; ++it) {
}

int AddressSpaceGraph::compare(const AddressSpaceGraph &b) const {
	return 0;
}

/// Extract pointers using the ObjectState's pointerMask.
void AddressSpaceGraph::extract_pointers(const klee::ObjectState &obj, 
		MemoryObjectNode *node) {

	for (unsigned i=0; i<obj.size; ++i) {
		if (obj.isBytePointer(i)) {
			for (unsigned j=0; j<pointer_width_/8; ++j) {
				assert(obj.isBytePointer(i+j) && "invalid pointer size");
			}
			klee::ref<klee::Expr> pexpr = obj.read(i, pointer_width_);
			if (klee::ConstantExpr *CE = llvm::dyn_cast<klee::ConstantExpr>(pexpr)) {
				klee::ObjectPair object_pair;
			  uint64_t val = CE->getZExtValue(pointer_width_);
				if (address_space_->resolveOne(CE, object_pair)) {
					PointerEdge *pe = new PointerEdge();
					pe->parent = node;
					pe->offset = i;
					pe->points_to_address = val;
					pe->points_to_object = object_pair.second;
					node->add_edge(pe);
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
void AddressSpaceGraph::extract_pointers_by_resolving(const klee::ObjectState &obj, 
		MemoryObjectNode *node) {

	// Attempt to resolve every 4 or 8 byte constant expr in the ObjectState
	for (unsigned i=0; i<obj.size; ++i) {
		klee::ref<klee::Expr> pexpr = obj.read(i, pointer_width_);
		if (klee::ConstantExpr *CE = dyn_cast<klee::ConstantExpr>(pexpr)) {
			klee::ObjectPair object_pair;
			if (address_space_->resolveOne(CE, object_pair)) {
				PointerEdge *pe = new PointerEdge();
				pe->parent = node;
				pe->offset = i;
				pe->points_to_address = CE->getZExtValue(pointer_width_);
				pe->points_to_object = object_pair.second;
				node->add_edge(pe);
			}
		}
	}
}

} // end namespace cliver

