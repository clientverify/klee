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

PointerEdge::PointerEdge () 
	: address(0), base_address(0), offset(0), points_to_address(0), 
	points_to_base_address(0), points_to_offset(0), points_to_node(NULL), next(NULL) {}
MemoryObjectNode::MemoryObjectNode() 
	: degree(0), base_address(0), object_state(NULL), first_edge(NULL), last_edge(NULL) {}

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
 : address_space_(address_space), pointer_width_(klee::Context::get().getPointerWidth()) {
	
}

void AddressSpaceGraph::build_graph() {

	for (klee::MemoryMap::iterator it=address_space_->objects.begin(),
			ie=address_space_->objects.end(); it!=ie; ++it) {
		MemoryObjectNode *mon = new MemoryObjectNode();
		extract_pointers(*it->second, mon);
		nodes_.push_back(mon);
	}
}

int AddressSpaceGraph::compare(const AddressSpaceGraph &b) const {
	return 0;
}

void AddressSpaceGraph::extract_pointers(const klee::ObjectState &obj, 
		MemoryObjectNode *node) {

	for (unsigned i=0; i<obj.size; ++i) {
		if (obj.isBytePointer(i)) {
			for (unsigned j=0; j<pointer_width_/8; ++j) {
				if (!obj.isBytePointer(i+j)) {
					cv_warning("AddressSpaceGraph: i=%d, j=%d, pointer_width=%d", i, j, pointer_width_/8);
					obj.print(*cv_message_stream);
				}
				assert(obj.isBytePointer(i+j) && "invalid pointer size");
			}
			klee::ref<klee::Expr> pointer_expr = obj.read(i, pointer_width_);
			if (klee::ConstantExpr *CE = llvm::dyn_cast<klee::ConstantExpr>(pointer_expr)) {
				klee::ObjectPair object_pair;
				if (address_space_->resolveOne(CE, object_pair)) {
					PointerEdge *pe = new PointerEdge();
					pe->offset = i;
					pe->points_to_address = CE->getZExtValue(pointer_width_);
					node->add_edge(pe);
					//cv_message("adding new edge at offset %d, points to %x", pe->offset, pe->points_to_address);
				}
			}
			// print warning when a symbolic pointer is found?
			i += (pointer_width_/8) - 1;
		}
	}
}

void AddressSpaceGraph::extract_pointers_by_resolving(const klee::ObjectState &obj, 
		MemoryObjectNode *node) {

	// Attempt to resolve every 4 or 8 byte constant expr in the ObjectState
	for (unsigned i=0; i<obj.size; ++i) {
		klee::ref<klee::Expr> pointer_expr = obj.read(i, pointer_width_);
		if (klee::ConstantExpr *CE = dyn_cast<klee::ConstantExpr>(pointer_expr)) {
			klee::ObjectPair object_pair;
			if (address_space_->resolveOne(CE, object_pair)) {
				PointerEdge *pe = new PointerEdge();
				pe->offset = i;
				pe->points_to_address = CE->getZExtValue(pointer_width_);
				pe->points_to_object = object_pair.second;
				node->add_edge(pe);
				//cv_message("adding new edge at offset %d, points to %x", pe->offset, pe->points_to_address);
			}
		}
	}
}

void AddressSpaceGraph::test_extract_pointers() {
	for (klee::MemoryMap::iterator it=address_space_->objects.begin(),
			ie=address_space_->objects.end(); it!=ie; ++it) {
		MemoryObjectNode *a = new MemoryObjectNode();
		MemoryObjectNode *b = new MemoryObjectNode();
		extract_pointers(*it->second, a);
		extract_pointers_by_resolving(*it->second, b);
		if (a->degree != b->degree) {
			cv_warning("AddressSpaceGraph Test: degree mismatch %d != %d",
					a->degree, b->degree);
			(*it->second).print(*cv_warning_stream);
			//PointerEdge *b_edge=b->first_edge;
			//while (b_edge != NULL) {
			//	*cv_warning_stream << "edge points_to object: ";
			//	if (b_edge->points_to_object != NULL) 
			//		b_edge->points_to_object->print(*cv_warning_stream,false);
			//	b_edge = b_edge->next;
			//}
			return;
		} else {
			PointerEdge *a_edge=a->first_edge, *b_edge=b->first_edge;
			while (a_edge != NULL && b_edge != NULL) {
				if (a_edge->offset != b_edge->offset) {
					cv_warning("AddressSpaceGraph Test: edge offset mismatch %d != %d",
						a_edge->offset, b_edge->offset);
					(*it->second).print(*cv_warning_stream);
					return;
				} 
				else if (a_edge->points_to_address != b_edge->points_to_address) {
					cv_warning("AddressSpaceGraph Test: edge points_to_address mismatch %ld != %ld",
						a_edge->points_to_address, b_edge->points_to_address);
					(*it->second).print(*cv_warning_stream);
					return;
				}
				a_edge = a_edge->next;
				b_edge = b_edge->next;
			}
		} 
		//if (equivalent && a->degree > 0) {
		//	cv_warning("AddressSpaceGraph Test: Pointers match, degree = %d", a->degree);
		//	(*it->second).print(*cv_warning_stream, false);
		//}
	}
	cv_message("test_extract_pointers: PASSED");
}


} // end namespace cliver

