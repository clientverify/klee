//===-- AddressSpaceGraph.cpp -----------------------------------*- C++ -*-===//
//
// <insert license>
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//

#include "klee/Internal/Module/Cell.h"
#include "klee/Internal/Module/KInstruction.h"
#include "klee/Internal/Module/KModule.h"
#include "../Core/Context.h"
#include "AddressSpaceGraph.h"
#include "CVStream.h"
#include "CVExecutionState.h"

#include <boost/foreach.hpp>
#define foreach BOOST_FOREACH 

namespace cliver {

llvm::cl::opt<bool>
DebugAddressSpaceGraph("debug-address-space-graph",llvm::cl::init(false));

#ifndef NDEBUG

#undef CVDEBUG
#define CVDEBUG(x) \
	__CVDEBUG(DebugAddressSpaceGraph, x);

#undef CVDEBUG_S
#define CVDEBUG_S(__state_id, __x) \
	__CVDEBUG_S(DebugAddressSpaceGraph, __state_id, __x)

#undef CVDEBUG_S2
#define CVDEBUG_S2(__state_id_1, __state_id_2, __x) \
	__CVDEBUG_S2(DebugAddressSpaceGraph, __state_id_1, __state_id_2, __x) \

#else

#undef CVDEBUG
#define CVDEBUG(x)

#undef CVDEBUG_S
#define CVDEBUG_S(__state_id, __x)

#undef CVDEBUG_S2
#define CVDEBUG_S2(__state_id_1, __state_id_2, __x)

#endif


AddressSpaceGraph::AddressSpaceGraph(klee::ExecutionState *state) 
 : state_(state), pointer_width_(klee::Context::get().getPointerWidth()) {
	cv_state_ = static_cast<CVExecutionState*>(state);
}

/// Compare the concrete values of two ObjectStates, ignoring pointers and symbolics.
bool AddressSpaceGraph::concrete_compare(klee::ObjectState &a, 
		klee::ObjectState &b) const{

	if (a.size != b.size)
		return false;

	// Check that the pointer masks are equal
	for (unsigned i=0; i<a.size; i++) {
		if ((a.isByteConcrete(i) != b.isByteConcrete(i)) ||
		    (a.isBytePointer(i) != b.isBytePointer(i))) {
			return false;
		}
	}

	// if concrete, and not a pointer, the concrete values must be equal
	for (unsigned i=0; i<a.size; i++) {
		if (!a.isBytePointer(i) && 
				a.isByteConcrete(i) && 
				a.read8(i) != b.read8(i)) {
			return false;
		}
	}
	return true;
}

/// Compare the concrete values of two ObjectStates, ignoring pointers 
bool AddressSpaceGraph::compare_objects(
    const AddressSpaceGraph &asg_b, 
		klee::ObjectState &a, 
		klee::ObjectState &b) const{

	if (a.size != b.size)
		return false;

	// Check that the pointer masks are equal
	for (unsigned i=0; i<a.size; i++) {
		if ((a.isByteConcrete(i) != b.isByteConcrete(i)) ||
		    (a.isBytePointer(i) != b.isBytePointer(i))) {
			return false;
		}
	}

	// ignore pointer values, but compare symbolics and concretes
	for (unsigned i=0; i<a.size; i++) {
		if (!a.isBytePointer(i)) {
			klee::ref<klee::Expr> a_expr = a.read8(i);
			klee::ref<klee::Expr> b_expr;

			if (klee::ConstantExpr *CE_b = dyn_cast<klee::ConstantExpr>(b.read8(i))) {
				b_expr = b.read8(i);
			} else {
				b_expr = get_canonical_expr(asg_b, b.read8(i));
			}

			if (a_expr != b_expr) {
				return false;
			}
		}
	}
	return true;
}

klee::ref<klee::Expr> AddressSpaceGraph::get_canonical_expr(
		const AddressSpaceGraph &b, klee::ref<klee::Expr> e) const {
	// TODO may miss comparison when array doesn't exist in map, and differs 
	// from the correspondong array in the other expr, but structurally are equivalent
	ReplaceArrayVisitor visitor(b.array_map_, in_order_arrays_);
	klee::ref<klee::Expr> new_e = visitor.visit(e);
	//CVDEBUG("Converted expr " << e << " to " << new_e );
	return new_e;
	//return visitor.visit(e);
}

bool AddressSpaceGraph::array_size_equal(const AddressSpaceGraph &b) const {

	int id_a = cv_state_->id(), id_b = b.cv_state_->id();

	if (array_map_.size() != b.array_map_.size()) {
		CVDEBUG_S2(id_a, id_b, "array map sizes differ " << array_map_.size()
			 << "	!= " << b.array_map_.size());
		return false;
	}

	return true;
}

bool AddressSpaceGraph::visited_size_equal(
		const AddressSpaceGraph &b) const {

	int id_a = cv_state_->id(), id_b = b.cv_state_->id();

	if (in_order_visited_.size() != b.in_order_visited_.size()) {
		CVDEBUG_S2(id_a, id_b, "vertex counts don't match");
		return false;
	}

	return true;
}

bool AddressSpaceGraph::unconnected_objects_equal(const AddressSpaceGraph &b) const {

	int id_a = cv_state_->id(), id_b = b.cv_state_->id();

	if (unconnected_.size() != b.unconnected_.size()) {
		CVDEBUG_S2(id_a, id_b, "compare global objects count failed");
		return false;
	}

	ObjectVertexMap::const_iterator it_a = unconnected_.begin();
	ObjectVertexMap::const_iterator ie_a = unconnected_.end();
	ObjectVertexMap::const_iterator it_b = b.unconnected_.begin();
	ObjectVertexMap::const_iterator ie_b = b.unconnected_.begin();

	while (it_a != ie_a && it_b != ie_b) {
		klee::ObjectState* object_state_a = it_a->first;
		klee::ObjectState* object_state_b = it_b->first;
		if (!compare_objects(b, *object_state_a, *object_state_b)) {
			CVDEBUG_S2(id_a, id_b, "compare global concrete objects failed"
					<< "\n" << ObjectStatePair(*object_state_a, *object_state_b));
			return false;
		}
		++it_a;
		++it_b;
	}
	return true;
}

bool AddressSpaceGraph::objects_equal(const AddressSpaceGraph &b) const {
	int id_a = cv_state_->id(), id_b = b.cv_state_->id();

	for (unsigned i=0; i<in_order_visited_.size(); ++i) {
		Vertex va = in_order_visited_[i], vb = b.in_order_visited_[i];

		klee::ObjectState* object_state_a
			= boost::get(boost::get(&VertexProperties::object, graph_), va);

		klee::ObjectState* object_state_b
			= boost::get(boost::get(&VertexProperties::object, b.graph_), vb);

		if (!compare_objects(b, *object_state_a, *object_state_b)) {
			CVDEBUG_S2(id_a, id_b, "compare objects failed"
					<< "\n" << ObjectStatePair(*object_state_a, *object_state_b));
			return false;
		}
	}

	return true;
}

bool AddressSpaceGraph::graphs_equal(const AddressSpaceGraph &b) const {

	int id_a = cv_state_->id(), id_b = b.cv_state_->id();

	std::map<Vertex, Vertex> a_to_b_map;

	for (unsigned i=0; i<in_order_visited_.size(); ++i) {
		Vertex va = in_order_visited_[i], vb = b.in_order_visited_[i];
		assert(a_to_b_map.find(va) == a_to_b_map.end());
		a_to_b_map[va] = vb;
	}

	for (unsigned i=0; i<in_order_visited_.size(); ++i) {
		Vertex va = in_order_visited_[i], vb = b.in_order_visited_[i];

		std::pair<OutEdgeIterator, OutEdgeIterator> out_edges_a 
			= boost::out_edges(va, graph_);
		std::pair<OutEdgeIterator, OutEdgeIterator> out_edges_b 
			= boost::out_edges(vb, b.graph_);

		while (out_edges_a.first != out_edges_a.second && 
				out_edges_b.first != out_edges_b.second) {

			Vertex target_a = boost::target(*(out_edges_a.first), graph_);
			Vertex target_b = boost::target(*(out_edges_b.first), b.graph_);
			if (a_to_b_map[target_a] != target_b) {
				CVDEBUG_S2(id_a, id_b, "compare out edges failed");
				return false;
			}

			++out_edges_a.first;
			++out_edges_b.first;
		}
	}
	return true;
}

bool AddressSpaceGraph::equal(const AddressSpaceGraph &b) const {

	if (!array_size_equal(b)) {
		return false;
	}

	if (!visited_size_equal(b)) {
		return false;
	}

	if (!unconnected_objects_equal(b)) {
		return false;
	}

	if (!objects_equal(b)) {
		return false;
	}

	if (!graphs_equal(b)) {
		return false;
	}

	return true;
}

bool AddressSpaceGraph::symbolic_equal(
		const AddressSpaceGraph &b, std::set<klee::ObjectState*> &objs) const {

	if (!array_size_equal(b)) {
		return false;
	}

	if (!visited_size_equal(b)) {
		return false;
	}

	if (!unconnected_objects_equal(b)) {
		return false;
	}

	for (unsigned vi=0; vi<in_order_visited_.size(); ++vi) {
		Vertex va = in_order_visited_[vi], vb = b.in_order_visited_[vi];

		klee::ObjectState* osa
			= boost::get(boost::get(&VertexProperties::object, graph_), va);

		klee::ObjectState* osb
			= boost::get(boost::get(&VertexProperties::object, b.graph_), vb);

		if (osa->size != osb->size)
			return false;

		// Check that the pointer masks are equal
		for (unsigned i=0; i<osa->size; i++) {
			if ((osa->isByteConcrete(i) != osb->isByteConcrete(i)) ||
					(osa->isBytePointer(i) != osb->isBytePointer(i))) {
				return false;
			}
		}

		// ignore pointer values, but compare symbolics and concretes
		bool candidate_sym_merge = false;
		for (unsigned i=0; i<osa->size; i++) {
			if (!osa->isBytePointer(i)) {
				klee::ref<klee::Expr> a_expr = osa->read8(i);
				klee::ref<klee::Expr> b_expr = osb->read8(i);
				klee::ConstantExpr *CE_a = dyn_cast<klee::ConstantExpr>(a_expr);
				klee::ConstantExpr *CE_b = dyn_cast<klee::ConstantExpr>(b_expr);

				if (CE_a != NULL && CE_b != NULL) {
					if (a_expr != b_expr) {
						candidate_sym_merge = true;
					}
				} else {
					klee::ref<klee::Expr> a_expr = osa->read8(i);
					klee::ref<klee::Expr> b_expr;
					if (CE_b) {
						b_expr = osb->read8(i);
					} else {
						b_expr = get_canonical_expr(b, osb->read8(i));
					}
					if (a_expr != b_expr) {
						return false;
					}
				}
			}
		}
		if (candidate_sym_merge) {
			objs.insert(osa);
		}
	}

	if (!graphs_equal(b)) {
		return false;
	}

	return true;
}


void AddressSpaceGraph::add_vertex(klee::ObjectState* object) {
	Vertex v = boost::add_vertex(graph_);
	graph_[v].object = object;
	object_vertex_map_[object] = v;
}
	
/// Build a graph on all the objects in the address space using pointer 
/// relationships as edges. 
void AddressSpaceGraph::build() {

	// Create a Vertex for each MemoryObject in the addressSpace.
	for (klee::MemoryMap::iterator it=state_->addressSpace.objects.begin(),
			ie=state_->addressSpace.objects.end(); it!=ie; ++it) {
		add_vertex(it->second);
	}
	
	// Create an edge between vertices for every pointer
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

	//// TODO test characteristics of the graphs, unconnected nodes, etc.
	//if (visited.size() != object_vertex_map_.size()) {
	//	cv_message("Orphan MO! %d != %d \n", 
	//			(int)visited.size(), (int)object_vertex_map_.size());
	//	foreach (ObjectVertexPair pair, object_vertex_map_) {
	//		Vertex v = pair.second;
	//		if (visited.find(v) == visited.end()) {
	//			cv_message("Vertex degree(in,out) = (%d, %d)", 
	//					(int)boost::in_degree(v,graph_), (int)boost::out_degree(v,graph_));
	//			klee::ObjectState* object_state 
	//				= boost::get(boost::get(&VertexProperties::object, graph_), v);
	//			object_state->print(*cv_debug_stream, false);
	//			//state_->addressSpace.unbindObject(object_state->getObject());
	//		}
	//	}
	//} else {
	//	cv_message("All nodes are reachable  %d == %d \n", 
	//			(int)visited.size(), (int)object_vertex_map_.size());
	//}
	
	process();
}

void AddressSpaceGraph::process() {
	std::set<Vertex> visited;
	AddressSpaceGraphVisitor graph_visitor(*this, visited);

	foreach (klee::StackFrame sf, state_->stack) {
		foreach (const klee::MemoryObject* mo, sf.allocas) {
			klee::ObjectPair object_pair;
			if (state_->addressSpace.resolveOne(mo->getBaseExpr(), object_pair)) {
				klee::ObjectState* object = const_cast<klee::ObjectState*>(object_pair.second);
				assert(object_vertex_map_.find(object) != object_vertex_map_.end());
				Vertex v = object_vertex_map_[object];
				boost::breadth_first_search(graph_, v, boost::visitor(graph_visitor));
			}
		}
		if (sf.locals) {
			for (unsigned i=0; i<sf.kf->numRegisters; ++i) {
				if (!sf.locals[i].value.isNull()) {
					add_arrays_from_expr(sf.locals[i].value);
				}
			}
		}
	}

	// Create a list of unconnected objects. 
	foreach (ObjectVertexPair pair, object_vertex_map_) {
		klee::ObjectState* object_state = pair.first;
		Vertex v = pair.second;
		if (visited.find(v) == visited.end()) {
			if (!object_state->getObject()->isGlobal) {
				*cv_debug_stream << *object_state << "\n";
				cv_error("non-global unconnected object");
			}
			for (unsigned i=0; i<object_state->size; ++i) {
				if(!object_state->isByteConcrete(i)) {
					cv_error("symbolic found in unconnected object");
				}
			}
			unconnected_.insert(pair);
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
				if (val) {
					if (state_->addressSpace.resolveOne(CE, object_pair)) {
						PointerProperties p;  
						p.offset = i;
						p.address = val;
						p.object = const_cast<klee::ObjectState*>(object_pair.second);
						results.push_back(p);
					} else {
						CVDEBUG_S(cv_state_->id(), "address " << *CE << " did not resolve");
					}
				}
			} else {
				CVDEBUG_S(cv_state_->id(), "Non-concrete pointer");
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

void AddressSpaceGraph::add_arrays_from_expr(klee::ref<klee::Expr> e) {
	std::vector< klee::ref<klee::ReadExpr> > reads;
	klee::findReads(e, true, reads);
	for (unsigned i = 0; i != reads.size(); ++i) {
		klee::ReadExpr *re = reads[i].get();
		const klee::Array *array = re->updates.root;
		if (!arrays_.count(array)) {
			arrays_.insert(array);
			in_order_arrays_.push_back(array);
			// record the index of this array
			array_map_[array] = in_order_arrays_.size() - 1;
		}
	}
}

} // end namespace cliver

