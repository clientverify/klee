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

llvm::cl::opt<bool>
AllowGlobalSymbolics("allow-global-symbolics",llvm::cl::init(true));


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

////////////////////////////////////////////////////////////////////////////////

AddressSpaceGraph::AddressSpaceGraph(klee::ExecutionState *state) 
 : state_(state), pointer_width_(klee::Context::get().getPointerWidth()) {
	cv_state_ = static_cast<CVExecutionState*>(state);
}

/// Compares two ObjectStates, ignoring pointer values and symbolics. Returns 
/// false if pointer masks or concrete values differ.
bool AddressSpaceGraph::objects_equal(klee::ObjectState &a, 
		klee::ObjectState &b) const{


	if (a.size != b.size) {
		CVDEBUG("object sizes differ: (b) " << a << " (b)" << b);
		return false;
	}

	for (unsigned i=0; i<a.size; i++) {

		// Check that the pointer and concrete masks are equal
		if ((a.isByteConcrete(i) != b.isByteConcrete(i)) ||
		    (a.isBytePointer(i) != b.isBytePointer(i))) {
			CVDEBUG("object masks differ: (b) " << a << " (b)" << b);
			return false;
		}

		// If concrete, and not a pointer, the concrete values must be equal
		if (!a.isBytePointer(i) && 
				a.isByteConcrete(i) && 
				a.read8(i) != b.read8(i)) {
			return false;
		}
	}
	return true;
}

/// Compares two ObjectStates, ignoring pointer values. Returns false if any 
/// concrete value, symbolic value, or pointer location differs. Uses 
/// AddressSpaceGraph to canonicalize the Array names in symbolic reads.
bool AddressSpaceGraph::objects_equal(const AddressSpaceGraph &asg_b, 
		klee::ObjectState &a, klee::ObjectState &b) const{

	int id_a = cv_state_->id(), id_b = asg_b.cv_state_->id();

	if (a.size != b.size) {
		CVDEBUG_S2(id_a, id_b, "object sizes differ: (b) " << a << " (b)" << b);
		return false;
	}

	for (unsigned i=0; i<a.size; i++) {

		// Check that the pointer and concrete masks are equal
		if ((a.isByteConcrete(i) != b.isByteConcrete(i)) ||
		    (a.isBytePointer(i) != b.isBytePointer(i))) {
			CVDEBUG_S2(id_a, id_b, "object masks differ: (b) " << a << " (b)" << b);
			return false;
		}

		// Ignore pointer values, but compare symbolics and concretes
		if (!a.isBytePointer(i)) {
			klee::ref<klee::Expr> a_expr = a.read8(i), b_expr = b.read8(i);

			if(NULL == dyn_cast<klee::ConstantExpr>(b.read8(i))) {
				b_expr = get_canonical_expr(asg_b, b.read8(i));
			}

			if (a_expr != b_expr) {
				CVDEBUG_S2(id_a, id_b, "objects differ: (b) " << a << " (b)" << b);
				return false;
			}

		}
	}
	return true;
}

/// Compares two ObjectStates, ignoring pointer values. Returns false if any 
/// concrete value, symbolic value, or pointer location differs. Uses 
/// AddressSpaceGraph to canonicalize the Array names in symbolic reads.
/// Output variable only_concrete is set to true if the two objects only differ
/// in concrete values but are otherwise equivalent. It is set to true if
/// objects are different in size, symbolics, pointer mask, OR if completely
/// equivalent.
bool AddressSpaceGraph::objects_equal(const AddressSpaceGraph &asg_b, 
		klee::ObjectState &a, klee::ObjectState &b, bool &only_concrete) const {

	int id_a = cv_state_->id(), id_b = asg_b.cv_state_->id();

	only_concrete = true;
	if (a.size != b.size) {
		CVDEBUG_S2(id_a, id_b, "object sizes differ: (b) " << a << " (b)" << b);
		return false;
	}

	for (unsigned i=0; i<a.size; i++) {

		// Check that the pointer masks are equal
		if ((a.isByteConcrete(i) != b.isByteConcrete(i)) ||
				(a.isBytePointer(i) != b.isBytePointer(i))) {
			CVDEBUG_S2(id_a, id_b, "object masks differ: (b) " << a << " (b)" << b);
			return false;
		}

		// Ignore pointer values, but compare symbolics and concretes
		if (!a.isBytePointer(i)) {
			klee::ref<klee::Expr> a_expr = a.read8(i), b_expr = b.read8(i);

			if (dyn_cast<klee::ConstantExpr>(a_expr) &&
					dyn_cast<klee::ConstantExpr>(b_expr)) {
				if (a_expr != b_expr) {
					only_concrete = false;
				}
			} 

			if(NULL == dyn_cast<klee::ConstantExpr>(b.read8(i))) {
				b_expr = get_canonical_expr(asg_b, b.read8(i));
			}

			if (a_expr != b_expr) {
				CVDEBUG_S2(id_a, id_b, "objects differ: (b) " << a << " (b)" << b);
				only_concrete = true;
				return false;
			}
		}
	}
	return true;
}

/// Visits all ReadExpr in the given expr and attempts to replace any symbolic
/// variables from b with the corresponding variables in this AddressSpaceGraph.
klee::ref<klee::Expr> AddressSpaceGraph::get_canonical_expr(
		const AddressSpaceGraph &b, klee::ref<klee::Expr> e) const {
	// TODO may miss comparison when array doesn't exist in map, and differs 
	// from the correspondong array in the other expr, but structurally are equivalent
	ReplaceArrayVisitor visitor(b.array_map_, in_order_arrays_);
	klee::ref<klee::Expr> new_e = visitor.visit(e);
	//CVDEBUG("Converted expr " << e << " to " << new_e );
	return new_e;
}

/// Returns true if array sizes are equal.
bool AddressSpaceGraph::array_size_equal(const AddressSpaceGraph &b) const {

	int id_a = cv_state_->id(), id_b = b.cv_state_->id();

	if (array_map_.size() != b.array_map_.size()) {
		CVDEBUG_S2(id_a, id_b, "array map sizes differ " << array_map_.size()
			 << "	!= " << b.array_map_.size());
		return false;
	}

	return true;
}

/// Returns true if AddressSpaceGraph b has stack values and pointer
/// locations equivalent to this AddressSpaceGraph. Pointer values are
/// ignored.
bool AddressSpaceGraph::locals_equal(const AddressSpaceGraph &b) const {

	int id_a = cv_state_->id(), id_b = b.cv_state_->id();

	if (locals_.size() != b.locals_.size()) {
		CVDEBUG_S2(id_a, id_b, "locals sizes differ " << locals_.size()
			 << "	!= " << b.locals_.size());
		return false;
	}

	for (unsigned i=0; i<locals_.size(); ++i) {
		klee::ref<klee::Expr> a_expr = locals_[i].first;
		klee::ref<klee::Expr> b_expr = b.locals_[i].first;
		klee::ObjectState* a_object = locals_[i].second;
		klee::ObjectState* b_object = b.locals_[i].second;
		if (!a_object && !b_object) {
			if (a_expr.isNull() || b_expr.isNull()) {
				if (a_expr.isNull() != a_expr.isNull()) {
					CVDEBUG_S2(id_a, id_b, "locals null mismatch");
					return false;
				}
			} else {
				if (NULL == dyn_cast<klee::ConstantExpr>(b_expr)) {
					b_expr = get_canonical_expr(b, b.locals_[i].first);
				}
				if (a_expr != b_expr) {
					CVDEBUG_S2(id_a, id_b, "locals not equal: " 
							<< a_expr << " != " << b_expr);
					return false;
				}
			}
		} else if (!a_object || !b_object) {
			CVDEBUG_S2(id_a, id_b, "locals not equal (pointer mismatch) ");
			return false;
		} 
	}

	return true;
}

/// Returns true if the pointers in the AddressSpaceGraph b stack point
/// to the same ObjectStates as this AddressSpaceGraph.
bool AddressSpaceGraph::local_objects_equal(const AddressSpaceGraph &b) const {

	int id_a = cv_state_->id(), id_b = b.cv_state_->id();

	if (locals_.size() != b.locals_.size()) {
		CVDEBUG_S2(id_a, id_b, "locals sizes differ " << locals_.size()
			 << "	!= " << b.locals_.size());
		return false;
	}

	for (unsigned i=0; i<locals_.size(); ++i) {
		klee::ObjectState* a_object = locals_[i].second;
		klee::ObjectState* b_object = b.locals_[i].second;
		if (a_object && b_object) {
			if (!objects_equal(b, *a_object, *b_object)) {
				CVDEBUG_S2(id_a, id_b, "compare local pointer objects failed");
				return false;
			}
		}
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

/// Returns true if the contents of all unconnected objects in b are equal
/// to the contents of this AddressSpaceGraph's unconnected objects, when
/// compared in the order first seen. Does not attempt to find an out of
/// order matching.
bool AddressSpaceGraph::unconnected_objects_equal(const AddressSpaceGraph &b) const {

	int id_a = cv_state_->id(), id_b = b.cv_state_->id();

	if (unconnected_.size() != b.unconnected_.size()) {
		CVDEBUG_S2(id_a, id_b, "compare global objects count failed");
		return false;
	}

	ObjectVertexMap::const_iterator it_a = unconnected_.begin();
	ObjectVertexMap::const_iterator ie_a = unconnected_.end();
	ObjectVertexMap::const_iterator it_b = b.unconnected_.begin();
	ObjectVertexMap::const_iterator ie_b = b.unconnected_.end();

	while (it_a != ie_a && it_b != ie_b) {
		klee::ObjectState* object_state_a = it_a->first;
		klee::ObjectState* object_state_b = it_b->first;
		if (!objects_equal(b, *object_state_a, *object_state_b)) {
			CVDEBUG_S2(id_a, id_b, "compare global concrete objects failed");
			return false;
		}
		++it_a;
		++it_b;
	}
	return true;
}

/// Returns true if the contents of all connected ObjectStates in b are equal
/// to the contents of this AddressSpaceGraph's connected objects, when
/// compared in the order first visited. 
bool AddressSpaceGraph::connected_objects_equal(const AddressSpaceGraph &b) const {
	int id_a = cv_state_->id(), id_b = b.cv_state_->id();

	for (unsigned i=0; i<in_order_visited_.size(); ++i) {
		Vertex va = in_order_visited_[i], vb = b.in_order_visited_[i];

		klee::ObjectState* object_state_a
			= boost::get(boost::get(&VertexProperties::object, graph_), va);

		klee::ObjectState* object_state_b
			= boost::get(boost::get(&VertexProperties::object, b.graph_), vb);

		if (!objects_equal(b, *object_state_a, *object_state_b)) {
			CVDEBUG_S2(id_a, id_b, "compare objects failed");
			return false;
		}
	}

	return true;
}

/// Returns true if the graph structure b is equal to the this 
/// AddressSpaceGraph's structure.
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

/// Returns true if b is equivalent.
bool AddressSpaceGraph::equal(const AddressSpaceGraph &b) const {

	if (!array_size_equal(b)) {
		return false;
	}

	if (!visited_size_equal(b)) {
		return false;
	}

	if (!locals_equal(b)) {
		return false;
	}

	if (!local_objects_equal(b)) {
		return false;
	}

	if (!unconnected_objects_equal(b)) {
		return false;
	}

	if (!connected_objects_equal(b)) {
		return false;
	}

	if (!graphs_equal(b)) {
		return false;
	}

	return true;
}

/// Returns true if b is equivalent. If the two AddressSpaceGraphs are equivalent
/// in all aspects except for the concrete contents of ObjectStates, these objects
/// will be added to the set non_equal_concretes.	
bool AddressSpaceGraph::equal(
		const AddressSpaceGraph &b, std::set<klee::ObjectState*> &_non_equal_concretes) const {
	std::set<klee::ObjectState*> non_equal_concretes;

	int id_a = cv_state_->id(), id_b = b.cv_state_->id();

	if (!array_size_equal(b)) {
		return false;
	}

	if (!visited_size_equal(b)) {
		return false;
	}

	if (!locals_equal(b)) {
		return false;
	}

	for (unsigned i=0; i<locals_.size(); ++i) {
		klee::ObjectState* os_a = locals_[i].second;
		klee::ObjectState* os_b = b.locals_[i].second;
		if (os_a && os_b) {
			bool only_concrete_differ = false;
			if (!objects_equal(b, *os_a, *os_b, only_concrete_differ)) {
				if (only_concrete_differ) {
					non_equal_concretes.insert(os_a);
				} else {
					CVDEBUG_S2(id_a, id_b, "compare local pointer objects failed");
					return false;
				}
			}
		}
	}

	if (!unconnected_objects_equal(b)) {
		return false;
	}

	for (unsigned vi=0; vi<in_order_visited_.size(); ++vi) {
		Vertex va = in_order_visited_[vi], vb = b.in_order_visited_[vi];

		klee::ObjectState* os_a
			= boost::get(boost::get(&VertexProperties::object, graph_), va);

		klee::ObjectState* os_b
			= boost::get(boost::get(&VertexProperties::object, b.graph_), vb);

		bool only_concrete_differ = false;
		if (!objects_equal(b, *os_a, *os_b, only_concrete_differ)) {
			if (only_concrete_differ) {
				non_equal_concretes.insert(os_a);
			} else {
				CVDEBUG_S2(id_a, id_b, "compare objects failed");
				return false;
			}
		}
	}

	if (!graphs_equal(b)) {
		return false;
	}

	foreach(klee::ObjectState* os, non_equal_concretes) {
		_non_equal_concretes.insert(os);
	}

	if (!non_equal_concretes.empty())
		return false;

	return true;
}

/// Add a vertex to the graph for the given ObjectState
void AddressSpaceGraph::add_vertex(klee::ObjectState* object) {
	assert(object_vertex_map_.find(object) == object_vertex_map_.end());
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
	
	process();
}

/// Computes the reachable vertices in the graph using ObjectStates on the stack
/// as root objects.
void AddressSpaceGraph::process() {
	std::set<Vertex> visited;
	AddressSpaceGraphVisitor graph_visitor(*this, visited);

	// Use the stack to determine reachable objects
	foreach (klee::StackFrame sf, state_->stack) {
		foreach (const klee::MemoryObject* mo, sf.allocas) {
			klee::ObjectPair object_pair;
			// Lookup MemoryObject in the address space
			if (state_->addressSpace.resolveOne(mo->getBaseExpr(), object_pair)) {
				klee::ObjectState* os = const_cast<klee::ObjectState*>(object_pair.second);
				// Visit all nodes from this object (if we haven't already)
				if (root_objects_.find(os) == root_objects_.end()) {
					assert(object_vertex_map_.find(os) != object_vertex_map_.end());
					Vertex v = object_vertex_map_[os];
					boost::breadth_first_search(graph_, v, boost::visitor(graph_visitor));
					root_objects_.insert(os);
				}
			} else {
				cv_error("couldn't resolving memory object on stack");
			}
		}

		if (sf.locals) {
			for (unsigned i=0; i<sf.kf->numRegisters; ++i) {
				klee::ObjectState* os = NULL;
				if (!sf.locals[i].value.isNull()) {
					add_arrays_from_expr(sf.locals[i].value);
					if (klee::ConstantExpr *CE 
							= dyn_cast<klee::ConstantExpr>(sf.locals[i].value)) {
						klee::ObjectPair object_pair;
						if (state_->addressSpace.resolveOne(CE, object_pair)) {
							os = const_cast<klee::ObjectState*>(object_pair.second);
							if (root_objects_.find(os) == root_objects_.end()) {
								assert(object_vertex_map_.find(os) != object_vertex_map_.end());
								Vertex v = object_vertex_map_[os];
								boost::breadth_first_search(graph_, v, boost::visitor(graph_visitor));
								root_objects_.insert(os);
							}
						}
					}
				}
				locals_.push_back(std::make_pair(sf.locals[i].value, os));
			}
		}
	}

	// Create a list of unconnected objects from those that are not reachable
	// from the stack
	foreach (ObjectVertexPair pair, object_vertex_map_) {
		klee::ObjectState* object_state = pair.first;
		Vertex v = pair.second;
		if (visited.find(v) == visited.end()) {
			if (!AllowGlobalSymbolics) {
				if (!object_state->getObject()->isGlobal) {
					*cv_debug_stream << *object_state << "\n";
					cv_error("non-global unconnected object");
				}
				for (unsigned i=0; i<object_state->size; ++i) {
					if(!object_state->isByteConcrete(i)) {
						object_state->print(*cv_debug_stream, true);
						*cv_debug_stream << "\n";
						cv_error("symbolic found in unconnected object");
					}
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

