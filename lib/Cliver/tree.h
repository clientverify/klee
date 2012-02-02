//	STL-like templated tree class.
//  Revised by: Xinlin Cao(xinlincao@gmail.com)
//  Revised from Kasper Peeters's implementation.(http://www.aei.mpg.de/~peekas/tree/)
/* 
   The tree.hh library for C++ provides an STL-like container 
   class for n-ary trees, templated over the data stored at the nodes. 
   Various types of iterators are provided (post-order,
   pre-order, and others). Where possible the access methods are
   compatible with the STL or alternative algorithms are available.

   This program is free software: you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation, either version 3 of the
   License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.


*/

#ifndef tree_hh_
#define tree_hh_

//#include <cassert>
#include <memory>
#include <stdexcept>
#include <iterator>
#include <set>
#include <queue>
#include <algorithm>
#include <stack>

#include <boost/static_assert.hpp>
#include <boost/serialization/base_object.hpp>
#include <boost/serialization/split_member.hpp>

// HP-style construct/destroy have gone from the standard,
// so here is a copy.

namespace kp {

	template <class T1, class T2>
	void constructor(T1* p, T2& val) 
	{
		new ((void *) p) T1(val);
	}

	template <class T1>
	void constructor(T1* p) 
	{
		new ((void *) p) T1;
	}

	template <class T1>
	void destructor(T1* p)
	{
		p->~T1();
	}

}

namespace cliver 
{

/// A node in the tree, combining links to other nodes as well as the actual data.
template<class T>
class tree_node_ { // size: 5*4=20 bytes (on 32 bit arch), can be reduced by 8.
public:
	tree_node_<T> *parent;
	tree_node_<T> *first_child, *last_child;
	tree_node_<T> *prev_sibling, *next_sibling;
	T data;

  // boost::serialization
  typedef tree_node_<T>* _node_ptr;
  friend class boost::serialization::access;
  template<class Archive>
  void save(Archive & ar, const unsigned int version) const
  {
    ar & data;
  }

  template<class Archive>
  void load(Archive & ar, const unsigned int version)
  {
    ar & data;
    first_child = next_sibling = prev_sibling = last_child = parent = 0;
  }

  BOOST_SERIALIZATION_SPLIT_MEMBER()
}; // __attribute__((packed));




template <class T, class tree_node_allocator = std::allocator<tree_node_<T> > >
class tree {
protected:
	typedef tree_node_<T> tree_node;
public:
	/// Value of the data stored at a node.
	typedef T value_type;

	class iterator_base;
	class pre_order_iterator;
	class post_order_iterator;
	class children_iterator;
	class leaf_iterator;

	tree();
	tree(const T&);
	tree(const tree<T, tree_node_allocator>&);
	~tree();
	tree<T,tree_node_allocator>& operator=(const tree<T, tree_node_allocator>&);
  
  // boost::serialization
  typedef tree_node_<T>* _node_ptr;
  friend class boost::serialization::access;
  template<class Archive>
  void save(Archive & ar, const unsigned int version) const
  {
    std::stack<_node_ptr> node_stack;
    node_stack.push(head->next_sibling);
    int head_children = iterator_base(head->next_sibling).number_of_children();
    ar & head_children;
    ar & head->next_sibling;

    while (!node_stack.empty()) {
      _node_ptr node = node_stack.top();
      _node_ptr pos = node->last_child;
      node_stack.pop();
      while (pos) {
        int nchildren = iterator_base(pos).number_of_children();
        ar & nchildren;
        ar & pos;
        node_stack.push(pos);
        pos = pos->prev_sibling;
      }
    }
  }

  template<class Archive>
  void load(Archive & ar, const unsigned int version)
  {
    std::stack<std::pair<int,_node_ptr > > node_stack;
    int head_children;
    _node_ptr head_next_sibling;

    ar & head_children;
    ar & head_next_sibling;

    node_stack.push(std::make_pair(head_children, head_next_sibling));
    while (!node_stack.empty()) {
      int nchildren = node_stack.top().first;
      _node_ptr parent = node_stack.top().second;
      node_stack.pop();

      _node_ptr curr_node = 0, prev_node = 0;
      int sub_nchildren;
      for (int i=0; i<nchildren; i++) {

        ar & sub_nchildren;
        ar & curr_node;
        node_stack.push(std::make_pair(sub_nchildren, curr_node));

        if (i == 0) 
          parent->last_child = curr_node;

        if (prev_node) 
          prev_node->prev_sibling = curr_node;

        curr_node->next_sibling = prev_node;
        curr_node->parent = parent;
        prev_node = curr_node;
      }
      parent->first_child = curr_node;
    }

    head->next_sibling = head_next_sibling;
    head->next_sibling->next_sibling = feet;
    head->next_sibling->prev_sibling = head;
  }

  BOOST_SERIALIZATION_SPLIT_MEMBER()

	/// Base class for iterators, only pointers stored, no traversal logic.
#ifdef __SGI_STL_PORT
	class iterator_base : public stlport::bidirectional_iterator<T, ptrdiff_t> {
#else
	class iterator_base {
#endif
	public:
		typedef T                               value_type;
		typedef T*                              pointer;
		typedef T&                              reference;
		typedef size_t                          size_type;
		typedef ptrdiff_t                       difference_type;
		typedef std::bidirectional_iterator_tag iterator_category;

		iterator_base();
		iterator_base(tree_node *);

		T&             operator*() const;
		T*             operator->() const;

		/// Number of children of the node pointed to by the iterator.
		unsigned int number_of_children() const;

		children_iterator begin_children_iterator() const;
		children_iterator end_children_iterator() const;

		tree_node *node;
	};

	/// Depth-first iterator, first accessing the node, then its children.
	class pre_order_iterator : public iterator_base { 
	public:
		pre_order_iterator();
		pre_order_iterator(tree_node *);

		bool    operator==(const pre_order_iterator&) const;
		bool    operator!=(const pre_order_iterator&) const;
		pre_order_iterator&  operator++();
		pre_order_iterator&  operator--();
		pre_order_iterator   operator++(int);
		pre_order_iterator   operator--(int);
		pre_order_iterator&  operator+=(unsigned int);
		pre_order_iterator&  operator-=(unsigned int);
	};

	/// Depth-first iterator, first accessing the children, then the node itself.
	class post_order_iterator : public iterator_base {
	public:
		post_order_iterator();
		post_order_iterator(tree_node *);

		bool    operator==(const post_order_iterator&) const;
		bool    operator!=(const post_order_iterator&) const;
		post_order_iterator&  operator++();
		post_order_iterator&  operator--();
		post_order_iterator   operator++(int);
		post_order_iterator   operator--(int);
		post_order_iterator&  operator+=(unsigned int);
		post_order_iterator&  operator-=(unsigned int);

		/// Set iterator to the first child as deep as possible down the tree.
		void descend_all();
	};

	/// Breadth-first iterator, using a queue
	class breadth_first_queued_iterator : public iterator_base {
	public:
		breadth_first_queued_iterator();
		breadth_first_queued_iterator(tree_node *);

		bool    operator==(const breadth_first_queued_iterator&) const;
		bool    operator!=(const breadth_first_queued_iterator&) const;
		breadth_first_queued_iterator&  operator++();
		breadth_first_queued_iterator   operator++(int);
		breadth_first_queued_iterator&  operator+=(unsigned int);

	private:
		std::queue<tree_node *> traversal_queue;
	};

	/// Iterator which traverses only the nodes which are siblings of each other.
	class children_iterator : public iterator_base {
	public:
		children_iterator();
		children_iterator(tree_node *);

		bool    operator==(const children_iterator&) const;
		bool    operator!=(const children_iterator&) const;
		children_iterator&  operator++();
		children_iterator&  operator--();
		children_iterator   operator++(int);
		children_iterator   operator--(int);
		children_iterator&  operator+=(unsigned int);
		children_iterator&  operator-=(unsigned int);

		tree_node *range_first() const;
		tree_node *range_last() const;
		tree_node *parent_;
	private:
		void set_parent_();
	};

	/// Iterator which traverses only the leaves.
	class leaf_iterator : public iterator_base {
	public:
		leaf_iterator();
		leaf_iterator(tree_node *, tree_node *top);

		bool    operator==(const leaf_iterator&) const;
		bool    operator!=(const leaf_iterator&) const;
		leaf_iterator&  operator++();
		leaf_iterator&  operator--();
		leaf_iterator   operator++(int);
		leaf_iterator   operator--(int);
		leaf_iterator&  operator+=(unsigned int);
		leaf_iterator&  operator-=(unsigned int);
	private:
		tree_node *top_node;
	};

	/// Return iterator to the beginning of the tree.
	inline pre_order_iterator   root() const;

	/// Return iterator to the beginning of the tree.
	inline pre_order_iterator   begin_pre_order_iterator(const iterator_base&) const;
	/// Return iterator to the end of the tree.
	inline pre_order_iterator   end_pre_order_iterator(const iterator_base&) const;
	/// Return post-order iterator to the beginning of the tree.
	post_order_iterator  begin_post_order_iterator(const iterator_base&) const;
	/// Return post-order end iterator of the tree.
	post_order_iterator  end_post_order_iterator(const iterator_base&) const;


	/// Return breadth-first iterator based on a iterator.
	breadth_first_queued_iterator begin_breadth_first_iterator(const iterator_base&) const;
	/// Return breadth-first end iterator.
	breadth_first_queued_iterator end_breadth_first_iterator(const iterator_base&) const;


	/// Return children iterator to the first child of given node.
	children_iterator     begin_children_iterator(const iterator_base&) const;
	/// Return children end iterator for children of given node.
	children_iterator     end_children_iterator(const iterator_base&) const;

	/// Return leaf iterator to the first leaf of the subtree at the given node.
	leaf_iterator   begin_leaf_iterator(const iterator_base& top) const;
	/// Return leaf end iterator for the subtree at the given node.
	leaf_iterator   end_leaf_iterator(const iterator_base& top) const;

	/// Return iterator to the parent of a node.
	pre_order_iterator   parent(const iterator_base&);
	/// Return iterator to the previous sibling of a node.
	pre_order_iterator   previous_sibling(const iterator_base&) const;
	/// Return iterator to the next sibling of a node.
	pre_order_iterator   next_sibling(const iterator_base&) const;

	/// Erase all nodes of the tree.
	void     clear();
	/// Erase element at position pointed to by iterator, return incremented iterator.
	template<typename iter> iter erase(iter);
	/// Erase all children of the node pointed to by iterator.
	void     erase_children(const iterator_base&);


	/// Short-hand to insert_before topmost node in otherwise empty tree.
	pre_order_iterator set_root(const T& x);

	/// Insert node as last/first child of node pointed to by position.
	pre_order_iterator append_child(const iterator_base& position, const T& x);
	pre_order_iterator prepend_child(const iterator_base& position, const T& x);

	/// Insert node as previous sibling of node pointed to by position.
	pre_order_iterator insert_before(iterator_base& position, const T& x);
	/// Insert node as next sibling of node pointed to by position.
	pre_order_iterator insert_after(const iterator_base& position, const T& x);

	/// set the data on position to be x.
	pre_order_iterator modify_data(const iterator_base& position, const T& x);
	
	///set X as the new head of the tree, the old head will be a child of new head.
	pre_order_iterator reparent_root(const T& x);

	/// Move 'source' node (plus its children) to become the next sibling of 'target'.
	pre_order_iterator move_after(const iterator_base& target, const iterator_base& source);
	/// Move 'source' node (plus its children) to become the previous sibling of 'target'.
	pre_order_iterator move_before(const iterator_base& target, const iterator_base& source);

	/// Count the total number of nodes.
	size_t   size() const;
	/// Count the total number of nodes below the indicated node (plus one).
	size_t   size(const iterator_base&) const;
	/// Check if tree is empty.
	bool     empty() const;
	/// Compute the depth to the root or to a fixed other iterator.
	int depth(const iterator_base&);
	int depth(const iterator_base&, const iterator_base&);
	/// Determine the maximal depth of the tree. An empty tree has max_depth=-1.
	int      max_depth() const;
	/// Determine the maximal depth of the tree with top node at the given position.
	int      max_depth(const iterator_base&) const;
	/// Count the number of children of node at position.
	unsigned int number_of_children(const iterator_base&);
	/// Determine whether the iterator is an 'end' iterator and thus not actually pointing to a node.
	bool     is_valid(const iterator_base&) const;

	/// Comparator class for iterators (compares pointer values; why doesn't this work automatically?)
	class iterator_base_less {
	public:
		bool operator()(const typename tree<T, tree_node_allocator>::iterator_base& one,
			const typename tree<T, tree_node_allocator>::iterator_base& two) const
		{
			return one.node < two.node;
		}
	};
	tree_node *head, *feet;    // head/feet are always dummy; if an iterator points to them it is invalid
private:
	tree_node_allocator alloc_;
	void head_initialise_();
	//copy the data from other.src to this.dst
	void construct_children_nodes_helper_(const pre_order_iterator& dst, const tree<T, tree_node_allocator>& other, const pre_order_iterator& src);
	void copy_(const tree<T, tree_node_allocator>& other);

	/// Comparator class for two nodes of a tree (used for sorting and searching).
	template<class StrictWeakOrdering>
	class compare_nodes {
	public:
		compare_nodes(StrictWeakOrdering comp) : comp_(comp) {};

		bool operator()(const tree_node *a, const tree_node *b) 
		{
			return comp_(a->data, b->data);
		}
	private:
		StrictWeakOrdering comp_;
	};
};



// Tree

template <class T, class tree_node_allocator>
tree<T, tree_node_allocator>::tree() 
{
	head_initialise_();
}

template <class T, class tree_node_allocator>
tree<T, tree_node_allocator>::tree(const T& x) 
{
	head_initialise_();
	set_root(x);
}

template <class T, class tree_node_allocator>
tree<T, tree_node_allocator>::~tree()
{
	clear();
	alloc_.deallocate(head,1);
	alloc_.deallocate(feet,1);
}

template <class T, class tree_node_allocator>
void tree<T, tree_node_allocator>::head_initialise_() 
{ 
	
	head = alloc_.allocate(1,0); // MSVC does not have default second argument 
	feet = alloc_.allocate(1,0);

	head->parent=0;
	head->first_child=0;
	head->last_child=0;
	head->prev_sibling=0; //head;
	head->next_sibling=feet; //head;

	feet->parent=0;
	feet->first_child=0;
	feet->last_child=0;
	feet->prev_sibling=head;
	feet->next_sibling=0;
}

template <class T, class tree_node_allocator>
tree<T,tree_node_allocator>& tree<T, tree_node_allocator>::operator=(const tree<T, tree_node_allocator>& other)
{
	if (this != &other)
	{
		copy_(other);
	}
	return (*this);
}

template <class T, class tree_node_allocator>
tree<T, tree_node_allocator>::tree(const tree<T, tree_node_allocator>& other)
{
	head_initialise_();
	copy_(other);
}

template <class T, class tree_node_allocator>
void tree<T, tree_node_allocator>::construct_children_nodes_helper_(const pre_order_iterator& dst, const tree<T, tree_node_allocator>& other, const pre_order_iterator& src)
{
	
	children_iterator childIter = other.begin_children_iterator(src);
	for(; childIter != other.end_children_iterator(other.root()); ++childIter)
	{
		pre_order_iterator new_dst = append_child(dst,(*childIter));
		pre_order_iterator new_src = other.begin_pre_order_iterator(childIter);
		construct_children_nodes_helper_(new_dst, other, new_src);
	}
}


template <class T, class tree_node_allocator>
void tree<T, tree_node_allocator>::copy_(const tree<T, tree_node_allocator>& other) 
{
	//check self
	if (this == &other)
	{
		assert(0);
		return;
	}
	clear();

	
	set_root(*other.root());
	construct_children_nodes_helper_(this->root(), other, other.root());
}

template <class T, class tree_node_allocator>
void tree<T, tree_node_allocator>::clear()
{
	if(head)
	{          
		while(head->next_sibling!=feet)
		{
			erase(pre_order_iterator(head->next_sibling));
		}
	}
}

template<class T, class tree_node_allocator> 
void tree<T, tree_node_allocator>::erase_children(const iterator_base& it)
{
	//	std::cout << "erase_children " << it.node << std::endl;
	if(it.node==0) return;

	tree_node *cur=it.node->first_child;
	tree_node *prev=0;

  if (cur) {
    std::stack<tree_node*> node_stack;
    node_stack.push(cur);
    while (!node_stack.empty()) {
      cur = node_stack.top();
      node_stack.pop();
      if (cur->first_child && cur->last_child) {
        for(children_iterator c = begin_children_iterator(cur),
            cend = end_children_iterator(cur); c!=cend; ++c) {
          node_stack.push(c.node);
        }
      }
      kp::destructor(&cur->data);
      alloc_.deallocate(cur,1);
      cur->first_child=0;
      cur->last_child=0;
    }
  }

	//while(cur!=0) {
	//	prev=cur;
	//	cur=cur->next_sibling;
	//	erase_children(pre_order_iterator(prev));
	//	kp::destructor(&prev->data);
	//	alloc_.deallocate(prev,1);
	//}

	it.node->first_child=0;
	it.node->last_child=0;
	//	std::cout << "exit" << std::endl;
}

template<class T, class tree_node_allocator> 
template<class iter>
iter tree<T, tree_node_allocator>::erase(iter it)
{
	tree_node *cur=it.node;
	assert(cur!=head);
	iter ret=it;
	++ret;


	erase_children(it);

	if(cur->prev_sibling==0) {
		cur->parent->first_child=cur->next_sibling;
	}
	else {
		cur->prev_sibling->next_sibling=cur->next_sibling;
	}
	
	if(cur->next_sibling==0) {
		cur->parent->last_child=cur->prev_sibling;
	}
	else {
		cur->next_sibling->prev_sibling=cur->prev_sibling;
	}

	kp::destructor(&cur->data);
	alloc_.deallocate(cur,1);
	return ret;
}

template <class T, class tree_node_allocator>
typename tree<T, tree_node_allocator>::pre_order_iterator tree<T, tree_node_allocator>::root() const
{
	
	return pre_order_iterator(head->next_sibling);
}


template <class T, class tree_node_allocator>
typename tree<T, tree_node_allocator>::pre_order_iterator tree<T, tree_node_allocator>::begin_pre_order_iterator(const iterator_base& pos) const
{
	
	return pre_order_iterator(pos.node);
}

template <class T, class tree_node_allocator>
typename tree<T, tree_node_allocator>::pre_order_iterator tree<T, tree_node_allocator>::end_pre_order_iterator(const iterator_base& pos) const
{
	
	return pre_order_iterator(pos.node->next_sibling);
}


template <class T, class tree_node_allocator>
typename tree<T, tree_node_allocator>::breadth_first_queued_iterator tree<T, tree_node_allocator>::begin_breadth_first_iterator(const iterator_base& pos) const
{
	
	return breadth_first_queued_iterator(pos.node);
}

template <class T, class tree_node_allocator>
typename tree<T, tree_node_allocator>::breadth_first_queued_iterator tree<T, tree_node_allocator>::end_breadth_first_iterator(const iterator_base& pos) const
{
	
	return breadth_first_queued_iterator();
}


template <class T, class tree_node_allocator>
typename tree<T, tree_node_allocator>::post_order_iterator tree<T, tree_node_allocator>::begin_post_order_iterator(const iterator_base& pos) const
{
	
	tree_node *tmp=pos.node;
	if(tmp!=feet) {
		while(tmp->first_child)
			tmp=tmp->first_child;
	}
	return post_order_iterator(tmp);
}

template <class T, class tree_node_allocator>
typename tree<T, tree_node_allocator>::post_order_iterator tree<T, tree_node_allocator>::end_post_order_iterator(const iterator_base& pos) const
{
	
	return post_order_iterator(pos.node->next_sibling);
}


template <class T, class tree_node_allocator>
typename tree<T, tree_node_allocator>::children_iterator tree<T, tree_node_allocator>::begin_children_iterator(const iterator_base& pos) const
{
	
	assert(pos.node!=0);
	if(pos.node->first_child==0) {
		return end_children_iterator(pos);
	}
	return pos.node->first_child;
}

template <class T, class tree_node_allocator>
typename tree<T, tree_node_allocator>::children_iterator tree<T, tree_node_allocator>::end_children_iterator(const iterator_base& pos) const
{
	
	
	children_iterator ret(0);
	ret.parent_=pos.node;
	return ret;
}

template <class T, class tree_node_allocator>
typename tree<T, tree_node_allocator>::leaf_iterator tree<T, tree_node_allocator>::begin_leaf_iterator(const iterator_base& top) const
{
	
	tree_node *tmp=top.node;
	while(tmp->first_child)
		tmp=tmp->first_child;
	return leaf_iterator(tmp, top.node);
}

template <class T, class tree_node_allocator>
typename tree<T, tree_node_allocator>::leaf_iterator tree<T, tree_node_allocator>::end_leaf_iterator(const iterator_base& top) const
{
	
	return leaf_iterator(top.node, top.node);
}

template <class T, class tree_node_allocator>
typename tree<T, tree_node_allocator>::pre_order_iterator tree<T, tree_node_allocator>::parent(const iterator_base& position) 
{
	
	assert(position.node!=0);
	return pre_order_iterator(position.node->parent);
}

template <class T, class tree_node_allocator>
typename tree<T, tree_node_allocator>::pre_order_iterator tree<T, tree_node_allocator>::previous_sibling(const iterator_base& position) const
{
	
	assert(position.node!=0);
	pre_order_iterator ret(position);
	ret.node=position.node->prev_sibling;
	return ret;
}

template <class T, class tree_node_allocator>
typename tree<T, tree_node_allocator>::pre_order_iterator tree<T, tree_node_allocator>::next_sibling(const iterator_base& position) const
{
	
	assert(position.node!=0);
	pre_order_iterator ret(position);
	ret.node=position.node->next_sibling;
	return ret;
}

template <class T, class tree_node_allocator>
typename tree<T, tree_node_allocator>::pre_order_iterator tree<T, tree_node_allocator>::append_child(const iterator_base& position, const T& x)
{
	// If your program fails here you probably used 'append_child' to add the top
	// node to an empty tree. From version 1.45 the top element should be added
	// using 'insert_before'. See the documentation for further information, and sorry about
	// the API change.
	assert(position.node!=head);
	assert(position.node);

	tree_node* tmp = alloc_.allocate(1,0);
	kp::constructor(&tmp->data, x);
	tmp->first_child=0;
	tmp->last_child=0;

	tmp->parent=position.node;
	if(position.node->last_child!=0) {
		position.node->last_child->next_sibling=tmp;
	}
	else {
		position.node->first_child=tmp;
	}
	tmp->prev_sibling=position.node->last_child;
	position.node->last_child=tmp;
	tmp->next_sibling=0;
	return pre_order_iterator(tmp);
}

template <class T, class tree_node_allocator>
typename tree<T, tree_node_allocator>::pre_order_iterator tree<T, tree_node_allocator>::prepend_child(const iterator_base& position, const T& x)
{
	assert(position.node!=head);
	assert(position.node);

	tree_node* tmp = alloc_.allocate(1,0);
	kp::constructor(&tmp->data, x);
	tmp->first_child=0;
	tmp->last_child=0;

	tmp->parent=position.node;
	if(position.node->first_child!=0) {
		position.node->first_child->prev_sibling=tmp;
	}
	else {
		position.node->last_child=tmp;
	}
	tmp->next_sibling=position.node->first_child;
	position.node->first_child=tmp;
	tmp->prev_sibling=0;
	return pre_order_iterator(tmp);
}


template <class T, class tree_node_allocator>
typename tree<T, tree_node_allocator>::pre_order_iterator tree<T, tree_node_allocator>::set_root(const T& x)
{
	assert(head->next_sibling==feet);
	
  pre_order_iterator poi(feet);
	return insert_before(poi, x);
}

template <class T, class tree_node_allocator>
typename tree<T, tree_node_allocator>::pre_order_iterator tree<T, tree_node_allocator>::insert_before(iterator_base& position, const T& x)
{
	if(position.node==0) {
		position.node=feet; // Backward compatibility: when calling insert_before on a null node,
		// insert_before before the feet.
	}
	tree_node* tmp = alloc_.allocate(1,0);
	kp::constructor(&tmp->data, x);
	tmp->first_child=0;
	tmp->last_child=0;

	tmp->parent=position.node->parent;
	tmp->next_sibling=position.node;
	tmp->prev_sibling=position.node->prev_sibling;
	position.node->prev_sibling=tmp;

	if(tmp->prev_sibling==0) {
		if(tmp->parent) // when inserting nodes at the head, there is no parent
			tmp->parent->first_child=tmp;
	}
	else
	{
		tmp->prev_sibling->next_sibling=tmp;
	}
	return pre_order_iterator(tmp);
}


template <class T, class tree_node_allocator>
typename tree<T, tree_node_allocator>::pre_order_iterator tree<T, tree_node_allocator>::insert_after(const iterator_base& position, const T& x)
{
	tree_node* tmp = alloc_.allocate(1,0);
	kp::constructor(&tmp->data, x);
	tmp->first_child=0;
	tmp->last_child=0;

	tmp->parent=position.node->parent;
	tmp->prev_sibling=position.node;
	tmp->next_sibling=position.node->next_sibling;
	position.node->next_sibling=tmp;

	if(tmp->next_sibling==0) {
		if(tmp->parent) // when inserting nodes at the head, there is no parent
			tmp->parent->last_child=tmp;
	}
	else {
		tmp->next_sibling->prev_sibling=tmp;
	}
	return pre_order_iterator(tmp);
}


template <class T, class tree_node_allocator>
typename tree<T, tree_node_allocator>::pre_order_iterator tree<T, tree_node_allocator>::modify_data(const iterator_base& position, const T& x)
{
	kp::destructor(&position.node->data);
	kp::constructor(&position.node->data, x);
	return position;
}

///set X as the new head of the tree, the old head will be a child of new head.
template <class T, class tree_node_allocator>
typename tree<T, tree_node_allocator>::pre_order_iterator tree<T, tree_node_allocator>::reparent_root(const T& x)
{
	if(head->next_sibling == feet)
	{
		return this->set_root(x);
	}
	else
	{
		//remember the old head
		tree_node *old_head_node = head->next_sibling;

		//create a new head
		pre_order_iterator poi(feet);
		insert_before(poi, x);

		//change the tree head
		tree_node *new_head_node = head->next_sibling->next_sibling;
		head->next_sibling = new_head_node;

		//change the new head
		new_head_node->first_child = old_head_node;
		new_head_node->last_child = old_head_node;
		new_head_node->next_sibling = feet;
		new_head_node->prev_sibling = head;

		//change the feet
		feet->prev_sibling = new_head_node;

		//change the old head
		old_head_node->next_sibling = 0;
		old_head_node->prev_sibling = 0;
		old_head_node->parent = new_head_node;

		return begin_pre_order_iterator(root());
	}
}

template <class T, class tree_node_allocator>
typename tree<T, tree_node_allocator>::pre_order_iterator tree<T, tree_node_allocator>::move_after(const iterator_base& target, const iterator_base& source)
{
	tree_node *dst=target.node;
	tree_node *src=source.node;
	assert(dst);
	assert(src);

	if(dst==src) return this->begin_pre_order_iterator(source);
	if(dst->next_sibling)
		if(dst->next_sibling==src) // already in the right spot
			return this->begin_pre_order_iterator(source);

	// take src out of the tree
	if(src->prev_sibling!=0) src->prev_sibling->next_sibling=src->next_sibling;
	else                     src->parent->first_child=src->next_sibling;
	if(src->next_sibling!=0) src->next_sibling->prev_sibling=src->prev_sibling;
	else                     src->parent->last_child=src->prev_sibling;

	// connect it to the new point
	if(dst->next_sibling!=0) dst->next_sibling->prev_sibling=src;
	else                     dst->parent->last_child=src;
	src->next_sibling=dst->next_sibling;
	dst->next_sibling=src;
	src->prev_sibling=dst;
	src->parent=dst->parent;
	return pre_order_iterator(src);
}

template <class T, class tree_node_allocator>
typename tree<T, tree_node_allocator>::pre_order_iterator tree<T, tree_node_allocator>::move_before(const iterator_base& target, const iterator_base& source)
{
	tree_node *dst=target.node;
	tree_node *src=source.node;
	assert(dst);
	assert(src);

	if(dst==src) return this->begin_pre_order_iterator(source);
	if(dst->prev_sibling)
		if(dst->prev_sibling==src) // already in the right spot
			return this->begin_pre_order_iterator(source);

	// take src out of the tree
	if(src->prev_sibling!=0) src->prev_sibling->next_sibling=src->next_sibling;
	else                     src->parent->first_child=src->next_sibling;
	if(src->next_sibling!=0) src->next_sibling->prev_sibling=src->prev_sibling;
	else                     src->parent->last_child=src->prev_sibling;

	// connect it to the new point
	if(dst->prev_sibling!=0) dst->prev_sibling->next_sibling=src;
	else                     dst->parent->first_child=src;
	src->prev_sibling=dst->prev_sibling;
	dst->prev_sibling=src;
	src->next_sibling=dst;
	src->parent=dst->parent;
	return pre_order_iterator(src);
}


template <class T, class tree_node_allocator>
size_t tree<T, tree_node_allocator>::size() const
{
	size_t i=0;
	pre_order_iterator it=begin_pre_order_iterator(root()), eit=end_pre_order_iterator(root());
	while(it!=eit) {
		++i;
		++it;
	}
	return i;
}

template <class T, class tree_node_allocator>
size_t tree<T, tree_node_allocator>::size(const iterator_base& top) const
{
	size_t i=0;
	pre_order_iterator it=top, eit=top;
	++eit;
	while(it!=eit) {
		++i;
		++it;
	}
	return i;
}

template <class T, class tree_node_allocator>
bool tree<T, tree_node_allocator>::empty() const
{
	pre_order_iterator it=begin_pre_order_iterator(), eit=end_pre_order_iterator();
	return (it==eit);
}

template <class T, class tree_node_allocator>
int tree<T, tree_node_allocator>::depth(const iterator_base& it) 
{
	tree_node* pos=it.node;
	assert(pos!=0);
	int ret=0;
	while(pos->parent!=0) {
		pos=pos->parent;
		++ret;
	}
	return ret;
}

template <class T, class tree_node_allocator>
int tree<T, tree_node_allocator>::depth(const iterator_base& it, const iterator_base& root) 
{
	tree_node* pos=it.node;
	assert(pos!=0);
	int ret=0;
	while(pos->parent!=0 && pos!=root.node) {
		pos=pos->parent;
		++ret;
	}
	return ret;
}

template <class T, class tree_node_allocator>
int tree<T, tree_node_allocator>::max_depth() const
{
	int maxd=-1;
	for(tree_node *it = head->next_sibling; it!=feet; it=it->next_sibling)
	{
		maxd=std::max(maxd, max_depth(it));
	}

	return maxd;
}


template <class T, class tree_node_allocator>
int tree<T, tree_node_allocator>::max_depth(const iterator_base& pos) const
{
	
	tree_node *tmp=pos.node;

	//if(tmp==0 || tmp==head || tmp==feet) return 1234;
	if(tmp==head || tmp==feet) return 1;
	if(tmp==0) return 2;

	int curdepth=0, maxdepth=0;
	while(true) { // try to walk the bottom of the tree
		while(tmp->first_child==0) 
		{	
			if(tmp==pos.node) return maxdepth;
			if(tmp->next_sibling==0) 
			{
				// try to walk up and then right again
				do {
					tmp=tmp->parent;
					if(tmp==0) return maxdepth;
					--curdepth;
				} while(tmp->next_sibling==0);
			}

			

			if(tmp==pos.node) return maxdepth;
			tmp=tmp->next_sibling;
		}

		tmp=tmp->first_child;
		++curdepth;
		maxdepth=std::max(curdepth, maxdepth);
	} 
}

template <class T, class tree_node_allocator>
unsigned int tree<T, tree_node_allocator>::number_of_children(const iterator_base& it) 
{
	tree_node *pos=it.node->first_child;
	if(pos==0) return 0;

	unsigned int ret=1;

	while((pos=pos->next_sibling))
		++ret;
	return ret;
}

template <class T, class tree_node_allocator>
bool tree<T, tree_node_allocator>::is_valid(const iterator_base& it) const
{
	if(it.node==0 || it.node==feet || it.node==head) return false;
	else return true;
}

// Iterator base

template <class T, class tree_node_allocator>
tree<T, tree_node_allocator>::iterator_base::iterator_base()
: node(0)
{
}

template <class T, class tree_node_allocator>
tree<T, tree_node_allocator>::iterator_base::iterator_base(tree_node *tn)
: node(tn)
{
}

template <class T, class tree_node_allocator>
T& tree<T, tree_node_allocator>::iterator_base::operator*() const
{
	return node->data;
}

template <class T, class tree_node_allocator>
T* tree<T, tree_node_allocator>::iterator_base::operator->() const
{
	return &(node->data);
}

template <class T, class tree_node_allocator>
bool tree<T, tree_node_allocator>::post_order_iterator::operator!=(const post_order_iterator& other) const
{
	if(other.node!=this->node) return true;
	else return false;
}

template <class T, class tree_node_allocator>
bool tree<T, tree_node_allocator>::post_order_iterator::operator==(const post_order_iterator& other) const
{
	if(other.node==this->node) return true;
	else return false;
}

template <class T, class tree_node_allocator>
bool tree<T, tree_node_allocator>::pre_order_iterator::operator!=(const pre_order_iterator& other) const
{
	if(other.node!=this->node) return true;
	else return false;
}

template <class T, class tree_node_allocator>
bool tree<T, tree_node_allocator>::pre_order_iterator::operator==(const pre_order_iterator& other) const
{
	if(other.node==this->node) return true;
	else return false;
}

template <class T, class tree_node_allocator>
bool tree<T, tree_node_allocator>::children_iterator::operator!=(const children_iterator& other) const
{
	if(other.node!=this->node) return true;
	else return false;
}

template <class T, class tree_node_allocator>
bool tree<T, tree_node_allocator>::children_iterator::operator==(const children_iterator& other) const
{
	if(other.node==this->node) return true;
	else return false;
}

template <class T, class tree_node_allocator>
bool tree<T, tree_node_allocator>::leaf_iterator::operator!=(const leaf_iterator& other) const
{
	if(other.node!=this->node) return true;
	else return false;
}

template <class T, class tree_node_allocator>
bool tree<T, tree_node_allocator>::leaf_iterator::operator==(const leaf_iterator& other) const
{
	if(other.node==this->node && other.top_node==this->top_node) return true;
	else return false;
}


template <class T, class tree_node_allocator>
typename tree<T, tree_node_allocator>::children_iterator tree<T, tree_node_allocator>::iterator_base::begin_children_iterator() const
{
	if(node->first_child==0) 
		return end_children_iterator();

	children_iterator ret(node->first_child);
	ret.parent_=this->node;
	return ret;
}

template <class T, class tree_node_allocator>
typename tree<T, tree_node_allocator>::children_iterator tree<T, tree_node_allocator>::iterator_base::end_children_iterator() const
{
	children_iterator ret(0);
	ret.parent_=node;
	return ret;
}

template <class T, class tree_node_allocator>
unsigned int tree<T, tree_node_allocator>::iterator_base::number_of_children() const
{
	tree_node *pos=node->first_child;
	if(pos==0) return 0;

	unsigned int ret=1;
	while(pos!=node->last_child) {
		++ret;
		pos=pos->next_sibling;
	}
	return ret;
}



// Pre-order iterator

template <class T, class tree_node_allocator>
tree<T, tree_node_allocator>::pre_order_iterator::pre_order_iterator() 
: iterator_base(0)
{
}

template <class T, class tree_node_allocator>
tree<T, tree_node_allocator>::pre_order_iterator::pre_order_iterator(tree_node *tn)
: iterator_base(tn)
{
}

template <class T, class tree_node_allocator>
typename tree<T, tree_node_allocator>::pre_order_iterator& tree<T, tree_node_allocator>::pre_order_iterator::operator++()
{
	assert(this->node!=0);
	if(this->node->first_child != 0) {
		this->node=this->node->first_child;
	}
	else {
		while(this->node->next_sibling==0) {
			this->node=this->node->parent;
			if(this->node==0)
				return *this;
		}
		this->node=this->node->next_sibling;
	}
	return *this;

	
}

template <class T, class tree_node_allocator>
typename tree<T, tree_node_allocator>::pre_order_iterator& tree<T, tree_node_allocator>::pre_order_iterator::operator--()
{
	assert(this->node!=0);
	if(this->node->prev_sibling) {
		this->node=this->node->prev_sibling;
		while(this->node->last_child)
			this->node=this->node->last_child;
	}
	else {
		this->node=this->node->parent;
		if(this->node==0)
			return *this;
	}
	return *this;

	
}

template <class T, class tree_node_allocator>
typename tree<T, tree_node_allocator>::pre_order_iterator tree<T, tree_node_allocator>::pre_order_iterator::operator++(int)
{
	pre_order_iterator copy = *this;
	++(*this);
	return copy;
}

template <class T, class tree_node_allocator>
typename tree<T, tree_node_allocator>::pre_order_iterator tree<T, tree_node_allocator>::pre_order_iterator::operator--(int)
{
	pre_order_iterator copy = *this;
	--(*this);
	return copy;
}

template <class T, class tree_node_allocator>
typename tree<T, tree_node_allocator>::pre_order_iterator& tree<T, tree_node_allocator>::pre_order_iterator::operator+=(unsigned int num)
{
	while(num>0) {
		++(*this);
		--num;
	}
	return (*this);
}

template <class T, class tree_node_allocator>
typename tree<T, tree_node_allocator>::pre_order_iterator& tree<T, tree_node_allocator>::pre_order_iterator::operator-=(unsigned int num)
{
	while(num>0) {
		--(*this);
		--num;
	}
	return (*this);
}



// Post-order iterator

template <class T, class tree_node_allocator>
tree<T, tree_node_allocator>::post_order_iterator::post_order_iterator() 
: iterator_base(0)
{
}

template <class T, class tree_node_allocator>
tree<T, tree_node_allocator>::post_order_iterator::post_order_iterator(tree_node *tn)
: iterator_base(tn)
{
}

template <class T, class tree_node_allocator>
typename tree<T, tree_node_allocator>::post_order_iterator& tree<T, tree_node_allocator>::post_order_iterator::operator++()
{
	assert(this->node!=0);
	if(this->node->next_sibling==0) {
		this->node=this->node->parent;
	}
	else {
		this->node=this->node->next_sibling;
		while(this->node->first_child)
			this->node=this->node->first_child;
	}
	return *this;
	
}

template <class T, class tree_node_allocator>
typename tree<T, tree_node_allocator>::post_order_iterator& tree<T, tree_node_allocator>::post_order_iterator::operator--()
{
	assert(this->node!=0);
	if(this->node->last_child==0) 
	{
		
		while(this->node->prev_sibling==0)
		{	
			this->node=this->node->parent;
		}
		this->node=this->node->prev_sibling;
	}
	else 
	{
		this->node=this->node->last_child;
	}
	return *this;
	
}

template <class T, class tree_node_allocator>
typename tree<T, tree_node_allocator>::post_order_iterator tree<T, tree_node_allocator>::post_order_iterator::operator++(int)
{
	post_order_iterator copy = *this;
	++(*this);
	return copy;
}

template <class T, class tree_node_allocator>
typename tree<T, tree_node_allocator>::post_order_iterator tree<T, tree_node_allocator>::post_order_iterator::operator--(int)
{
	post_order_iterator copy = *this;
	--(*this);
	return copy;
}


template <class T, class tree_node_allocator>
typename tree<T, tree_node_allocator>::post_order_iterator& tree<T, tree_node_allocator>::post_order_iterator::operator+=(unsigned int num)
{
	while(num>0) {
		++(*this);
		--num;
	}
	return (*this);
}

template <class T, class tree_node_allocator>
typename tree<T, tree_node_allocator>::post_order_iterator& tree<T, tree_node_allocator>::post_order_iterator::operator-=(unsigned int num)
{
	while(num>0) {
		--(*this);
		--num;
	}
	return (*this);
}

template <class T, class tree_node_allocator>
void tree<T, tree_node_allocator>::post_order_iterator::descend_all()
{
	assert(this->node!=0);
	while(this->node->first_child)
		this->node=this->node->first_child;
}


// Breadth-first iterator

template <class T, class tree_node_allocator>
tree<T, tree_node_allocator>::breadth_first_queued_iterator::breadth_first_queued_iterator()
: iterator_base()
{
}

template <class T, class tree_node_allocator>
tree<T, tree_node_allocator>::breadth_first_queued_iterator::breadth_first_queued_iterator(tree_node *tn)
: iterator_base(tn)
{
	traversal_queue.push(tn);
}

template <class T, class tree_node_allocator>
bool tree<T, tree_node_allocator>::breadth_first_queued_iterator::operator!=(const breadth_first_queued_iterator& other) const
{
	if(other.node!=this->node) return true;
	else return false;
}

template <class T, class tree_node_allocator>
bool tree<T, tree_node_allocator>::breadth_first_queued_iterator::operator==(const breadth_first_queued_iterator& other) const
{
	if(other.node==this->node) return true;
	else return false;
}

template <class T, class tree_node_allocator>
typename tree<T, tree_node_allocator>::breadth_first_queued_iterator& tree<T, tree_node_allocator>::breadth_first_queued_iterator::operator++()
{
	assert(this->node!=0);

	// Add child nodes and pop current node
	children_iterator sib=this->begin_children_iterator();
	while(sib!=this->end_children_iterator()) {
		traversal_queue.push(sib.node);
		++sib;
	}
	traversal_queue.pop();
	if(traversal_queue.size()>0)
		this->node=traversal_queue.front();
	else 
		this->node=0;
	return (*this);
}

template <class T, class tree_node_allocator>
typename tree<T, tree_node_allocator>::breadth_first_queued_iterator tree<T, tree_node_allocator>::breadth_first_queued_iterator::operator++(int)
{
	breadth_first_queued_iterator copy = *this;
	++(*this);
	return copy;
}

template <class T, class tree_node_allocator>
typename tree<T, tree_node_allocator>::breadth_first_queued_iterator& tree<T, tree_node_allocator>::breadth_first_queued_iterator::operator+=(unsigned int num)
{
	while(num>0) {
		++(*this);
		--num;
	}
	return (*this);
}

// Sibling iterator

template <class T, class tree_node_allocator>
tree<T, tree_node_allocator>::children_iterator::children_iterator() 
: iterator_base()
{
	set_parent_();
}

template <class T, class tree_node_allocator>
tree<T, tree_node_allocator>::children_iterator::children_iterator(tree_node *tn)
: iterator_base(tn)
{
	set_parent_();
}

template <class T, class tree_node_allocator>
void tree<T, tree_node_allocator>::children_iterator::set_parent_()
{
	parent_=0;
	if(this->node==0) return;
	if(this->node->parent!=0)
		parent_=this->node->parent;
}

template <class T, class tree_node_allocator>
typename tree<T, tree_node_allocator>::children_iterator& tree<T, tree_node_allocator>::children_iterator::operator++()
{
	if(this->node)
		this->node=this->node->next_sibling;
	return *this;
}

template <class T, class tree_node_allocator>
typename tree<T, tree_node_allocator>::children_iterator& tree<T, tree_node_allocator>::children_iterator::operator--()
{
	if(this->node) this->node=this->node->prev_sibling;
	else {
		assert(parent_);
		this->node=parent_->last_child;
	}
	return *this;
}

template <class T, class tree_node_allocator>
typename tree<T, tree_node_allocator>::children_iterator tree<T, tree_node_allocator>::children_iterator::operator++(int)
{
	children_iterator copy = *this;
	++(*this);
	return copy;
}

template <class T, class tree_node_allocator>
typename tree<T, tree_node_allocator>::children_iterator tree<T, tree_node_allocator>::children_iterator::operator--(int)
{
	children_iterator copy = *this;
	--(*this);
	return copy;
}

template <class T, class tree_node_allocator>
typename tree<T, tree_node_allocator>::children_iterator& tree<T, tree_node_allocator>::children_iterator::operator+=(unsigned int num)
{
	while(num>0) {
		++(*this);
		--num;
	}
	return (*this);
}

template <class T, class tree_node_allocator>
typename tree<T, tree_node_allocator>::children_iterator& tree<T, tree_node_allocator>::children_iterator::operator-=(unsigned int num)
{
	while(num>0) {
		--(*this);
		--num;
	}
	return (*this);
}

template <class T, class tree_node_allocator>
typename tree<T, tree_node_allocator>::tree_node *tree<T, tree_node_allocator>::children_iterator::range_first() const
{
	tree_node *tmp=parent_->first_child;
	return tmp;
}

template <class T, class tree_node_allocator>
typename tree<T, tree_node_allocator>::tree_node *tree<T, tree_node_allocator>::children_iterator::range_last() const
{
	return parent_->last_child;
}

// Leaf iterator

template <class T, class tree_node_allocator>
tree<T, tree_node_allocator>::leaf_iterator::leaf_iterator() 
: iterator_base(0), top_node(0)
{
}

template <class T, class tree_node_allocator>
tree<T, tree_node_allocator>::leaf_iterator::leaf_iterator(tree_node *tn, tree_node *top)
: iterator_base(tn), top_node(top)
{
}


template <class T, class tree_node_allocator>
typename tree<T, tree_node_allocator>::leaf_iterator& tree<T, tree_node_allocator>::leaf_iterator::operator++()
{
	assert(this->node!=0);
	if(this->node->first_child!=0) { // current node is no longer leaf (children got added)
		while(this->node->first_child) 
			this->node=this->node->first_child;
	}
	else {
		while(this->node->next_sibling==0) { 
			if (this->node->parent==0) return *this;
			this->node=this->node->parent;
			if (top_node != 0 && this->node==top_node) return *this;
		}
		this->node=this->node->next_sibling;
		while(this->node->first_child)
			this->node=this->node->first_child;
	}
	return *this;
	
}

template <class T, class tree_node_allocator>
typename tree<T, tree_node_allocator>::leaf_iterator& tree<T, tree_node_allocator>::leaf_iterator::operator--()
{
	assert(this->node!=0);
	while (this->node->prev_sibling==0) {
		if (this->node->parent==0) return *this;
		this->node=this->node->parent;
		if (top_node !=0 && this->node==top_node) return *this; 
	}
	this->node=this->node->prev_sibling;
	while(this->node->last_child)
		this->node=this->node->last_child;
	return *this;
}

template <class T, class tree_node_allocator>
typename tree<T, tree_node_allocator>::leaf_iterator tree<T, tree_node_allocator>::leaf_iterator::operator++(int)
{
	leaf_iterator copy = *this;
	++(*this);
	return copy;
}

template <class T, class tree_node_allocator>
typename tree<T, tree_node_allocator>::leaf_iterator tree<T, tree_node_allocator>::leaf_iterator::operator--(int)
{
	leaf_iterator copy = *this;
	--(*this);
	return copy;
}


template <class T, class tree_node_allocator>
typename tree<T, tree_node_allocator>::leaf_iterator& tree<T, tree_node_allocator>::leaf_iterator::operator+=(unsigned int num)
{
	while(num>0) {
		++(*this);
		--num;
	}
	return (*this);
}

template <class T, class tree_node_allocator>
typename tree<T, tree_node_allocator>::leaf_iterator& tree<T, tree_node_allocator>::leaf_iterator::operator-=(unsigned int num)
{
	while(num>0) {
		--(*this);
		--num;
	}
	return (*this);
}

}

#endif
