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
#include "llvm/Support/CommandLine.h"

#include <boost/foreach.hpp>
#define foreach BOOST_FOREACH 

namespace cliver {

llvm::cl::opt<bool>
DebugAddressSpaceGraph("debug-address-space-graph",llvm::cl::init(false));

#ifndef NDEBUG

#define CVDEBUG_S2(__state_id_1, __state_id_2, __x) \
	if (DebugAddressSpaceGraph) { \
	*cv_debug_stream <<"CV: DEBUG ("<< __FILE__ <<":"<< __LINE__  <<") States: (" \
   <<  __state_id_1 << ", " << __state_id_2 << ") " <<__x << "\n"; }

#undef CVDEBUG_S
#define CVDEBUG_S(__state_id, __x) \
	if (DebugAddressSpaceGraph) { \
	*cv_debug_stream <<"CV: DEBUG ("<< __FILE__ <<":"<< __LINE__  <<") State: " \
   << std::setw(4) << std::right << __state_id << " - " << __x << "\n"; }

#else

#define CVDEBUG_S2(__state_id_1, __state_id_2, __x)

#undef CVDEBUG_S
#define CVDEBUG_S(__state_id, __x)

#endif

typedef std::pair<klee::ObjectState&,klee::ObjectState&> ObjectStatePair;

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

bool AddressSpaceGraph::equals(const AddressSpaceGraph &b) const {

	int id_a = cv_state_->id(), id_b = b.cv_state_->id();
	std::vector<klee::StackFrame>::const_iterator itA = state_->stack.begin();
	std::vector<klee::StackFrame>::const_iterator itB = b.state_->stack.begin();
	while (itA!=state_->stack.end() && itB!=b.state_->stack.end()) {
		if (itA->caller!=itB->caller || itA->kf!=itB->kf) {
			CVDEBUG_S2(id_a, id_b, "call stacks don't match");
			return false;
		}
		++itA;
		++itB;
	}
	if (itA!=state_->stack.end() || itB!=b.state_->stack.end()) {
		CVDEBUG_S2(id_a, id_b, "stack sizes don't match");
		return false;
	}

	if (in_order_visited_.size() != b.in_order_visited_.size()) {
		CVDEBUG_S2(id_a, id_b, "vertex counts don't match");
		return false;
	}

	std::map<Vertex, Vertex> a_to_b_map;

	for (unsigned i=0; i<in_order_visited_.size(); ++i) {
		Vertex va = in_order_visited_[i], vb = b.in_order_visited_[i];

		klee::ObjectState* object_state_a
			= boost::get(boost::get(&VertexProperties::object, graph_), va);

		klee::ObjectState* object_state_b
			= boost::get(boost::get(&VertexProperties::object, b.graph_), vb);

		if (!concrete_compare(*object_state_a, *object_state_b)) {
			CVDEBUG_S2(id_a, id_b, "compare concrete failed"
					<< "\n" << ObjectStatePair(*object_state_a, *object_state_b));
			return false;
		}
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

	if (unconnected_objects_.size() != b.unconnected_objects_.size()) {
		CVDEBUG_S2(id_a, id_b, "compare global objects count failed");
		return false;
	}

	ObjectVertexMap::const_iterator it_a = unconnected_objects_.begin();
	ObjectVertexMap::const_iterator ie_a = unconnected_objects_.end();
	ObjectVertexMap::const_iterator it_b = b.unconnected_objects_.begin();
	ObjectVertexMap::const_iterator ie_b = b.unconnected_objects_.begin();

	while (it_a != ie_a && it_b != ie_b) {
		klee::ObjectState* object_state_a = it_a->first;
		klee::ObjectState* object_state_b = it_b->first;
		if (!concrete_compare(*object_state_a, *object_state_b)) {
			CVDEBUG_S2(id_a, id_b, "compare global concrete objects failed"
					<< "\n" << ObjectStatePair(*object_state_a, *object_state_b));
			return false;
		}
		++it_a;
		++it_b;
	}

	// TODO check unnconnected!! 

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
	
	std::set<Vertex> visited;
	BFSVisitor bfs_vis(&visited, &in_order_visited_, &arrays_);

	// Use a BFS Visitor to determine all of the reachable MemoryObjects. BFSVisitor
	// also extracts all symbolic reads to create a list of arrays.
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
		if (sf.locals) {
			for (unsigned i=0; i<sf.kf->numRegisters; ++i) {
				if (!sf.locals[i].value.isNull()) {
					arrays_.add(klee::IndependentElementSet(sf.locals[i].value));
				}
			}
		}
	}

	// Create a list of connected/unconnected objects. All unconnected objects
	// should be global.
	foreach (ObjectVertexPair pair, object_vertex_map_) {
		klee::ObjectState* object_state = pair.first;
		Vertex v = pair.second;
		if (visited.find(v) == visited.end()) {
			if (!object_state->getObject()->isGlobal)
				cv_error("non-global unconnected object");
			for (unsigned i=0; i<object_state->size; ++i) {
				if(!object_state->isByteConcrete(i)) {
					cv_error("symbolic found in unconnected object");
				}
			}
			unconnected_objects_.insert(pair);
		} else {
			connected_objects_.insert(pair);
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

} // end namespace cliver

