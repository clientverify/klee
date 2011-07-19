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

#include <boost/foreach.hpp>
#define foreach BOOST_FOREACH 

namespace cliver {

AddressSpaceGraph::AddressSpaceGraph(klee::ExecutionState *state) 
 : state_(state), pointer_width_(klee::Context::get().getPointerWidth()) {}

/// Compare the concrete values of two ObjectStates, ignoring pointers and symbolics.
bool AddressSpaceGraph::compare_concrete(klee::ObjectState *a, klee::ObjectState *b) {

	if (a->size != b->size)
		return false;

	// Check that the pointer masks are equal
	for (unsigned i=0; i<a->size; i++) {
		if ((a->isByteConcrete(i) != b->isByteConcrete(i)) ||
		    (a->isBytePointer(i) != b->isBytePointer(i))) {
			return false;
		}
	}

	// if concrete, and not a pointer, the concrete values must be equal
	for (unsigned i=0; i<a->size; i++) {
		if (!a->isBytePointer(i) && 
				a->isByteConcrete(i) && 
				a->read8(i) != b->read8(i)) {
			return false;
		}
	}
	return true;
}

int AddressSpaceGraph::compare(const AddressSpaceGraph &b) const {

	// TODO Identify Rings
	return 0;
}

void AddressSpaceGraph::add_vertex(klee::ObjectState* object) {
	Vertex v = boost::add_vertex(graph_);
	graph_[v].object = object;
	object_vertex_map_[object] = v;
}
	
/// Build a graph on all the objects in the address space using pointer 
/// relationships as edges. 
void AddressSpaceGraph::build_graph() {

	// Create a Vertex for each MemoryObject in the addressSpace.
	for (klee::MemoryMap::iterator it=state_->addressSpace.objects.begin(),
			ie=state_->addressSpace.objects.end(); it!=ie; ++it) {
		add_vertex(it->second);
	}
	
	foreach (ObjectVertexPair pair, object_vertex_map_) {
		PointerList results;
		klee::ObjectState* object_state = pair.first;
		Vertex v = pair.second;
		extract_pointers(object_state, results);

		foreach (PointerProperties pointer, results) {
			assert(object_vertex_map_.find(pointer.object) != object_vertex_map_.end());
			Vertex v2 = object_vertex_map_[pointer.object];
			boost::add_edge(v, v2, pointer, graph_);
		}
	}
	
	std::set<Vertex> visited;
	BFSVisitor bfs_vis(&visited);

	foreach (klee::StackFrame sf, state_->stack) {
		foreach (const klee::MemoryObject* mo, sf.allocas) {
			klee::ObjectPair object_pair;
			if (state_->addressSpace.resolveOne(mo->getBaseExpr(), object_pair)) {
				klee::ObjectState* object = const_cast<klee::ObjectState*>(object_pair.second);
				stack_objects_.push_back(object);
				Vertex v = object_vertex_map_[object];
				boost::breadth_first_search(graph_, v, boost::visitor(bfs_vis));
			}
		}
	}
	if (visited.size() != object_vertex_map_.size()) {
		cv_message("Orphan MO! %d != %d \n", 
				(int)visited.size(), (int)object_vertex_map_.size());
		foreach (ObjectVertexPair pair, object_vertex_map_) {
			PointerList results;
			Vertex v = pair.second;
			if (visited.find(v) == visited.end()) {
				cv_message("Vertex degree(in,out) = (%d, %d)", 
						(int)boost::in_degree(v,graph_), (int)boost::out_degree(v,graph_));
				klee::ObjectState* object_state 
					= boost::get(boost::get(&VertexProperties::object, graph_), v);
				object_state->print(*cv_debug_stream, false);
			}
		}
	}

}

/// Extract pointers using the ObjectState's pointerMask.
void AddressSpaceGraph::extract_pointers(klee::ObjectState *obj, PointerList &results) {
	for (unsigned i=0; i<obj->size; ++i) {
		if (obj->isBytePointer(i)) {
			for (unsigned j=0; j<pointer_width_/8; ++j) {
				assert(obj->isBytePointer(i+j) && "invalid pointer size");
			}
			klee::ref<klee::Expr> pexpr = obj->read(i, pointer_width_);
			if (klee::ConstantExpr *CE = llvm::dyn_cast<klee::ConstantExpr>(pexpr)) {
				klee::ObjectPair object_pair;
			  uint64_t val = CE->getZExtValue(pointer_width_);
				if (state_->addressSpace.resolveOne(CE, object_pair)) {
					PointerProperties p;  
					p.offset = i;
					p.address = val;
					p.object = const_cast<klee::ObjectState*>(object_pair.second);
					results.push_back(p);
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
void AddressSpaceGraph::extract_pointers_by_resolving(klee::ObjectState *obj, 
		PointerList &results) {

	// Attempt to resolve every 4 or 8 byte constant expr in the ObjectState
	for (unsigned i=0; i<obj->size; ++i) {
		klee::ref<klee::Expr> pexpr = obj->read(i, pointer_width_);
		if (klee::ConstantExpr *CE = dyn_cast<klee::ConstantExpr>(pexpr)) {
			klee::ObjectPair object_pair;
			if (state_->addressSpace.resolveOne(CE, object_pair)) {
				PointerProperties p;  
				p.offset = i;
				p.address = CE->getZExtValue(pointer_width_);
				p.object = const_cast<klee::ObjectState*>(object_pair.second);
				results.push_back(p);
			}
		}
	}
}

} // end namespace cliver

