//===-- KExtensionTree.h  ---------------------------------------*- C++ -*-===//
//
// <insert license>
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//
#ifndef CLIVER_K_EXTENSION_TREE_H
#define CLIVER_K_EXTENSION_TREE_H

#include "cliver/RadixTree.h"
#include "cliver/EditDistanceTree.h"
#include "cliver/util/MurmurHash3.h"
#include <limits.h>

#include <vector>
#include <set>
#include <queue>

////////////////////////////////////////////////////////////////////////////////

namespace cliver {

////////////////////////////////////////////////////////////////////////////////

template <class Sequence, class Element> 
class KExtensionOptTree 
: public RadixTree<Sequence, Element>,
  public EditDistanceTree<Sequence,Element> {

  struct EdgeOffset;
  struct EdgeOffsetHash;

  typedef RadixTree<Sequence, Element> This;
  typedef typename This::Node Node;
  typedef typename This::Edge Edge;
  typedef typename This::SequenceIterator EdgeIterator;
  typedef typename This::EdgeMapIterator EdgeMapIterator;

  //typedef std::pair<Edge*, size_t> EdgeOffset;
  //typedef boost::unordered_map<EdgeOffset, int> EdgeOffsetMap;
  typedef boost::unordered_set<EdgeOffset, EdgeOffsetHash> EdgeOffsetMap;
  typedef typename EdgeOffsetMap::iterator EdgeOffsetMapIterator;
  typedef typename EdgeOffsetMap::value_type EdgeOffsetMapValue;
  typedef std::vector<EdgeOffsetMapIterator> EdgeOffsetList;

#define foreach_edge(__node, __iterator) \
  EdgeMapIterator __iterator = __node->begin(); \
  EdgeMapIterator __iterator##_END = __node->end(); \
  for (; __iterator != __iterator##_END; ++__iterator)

 public:

  //===-------------------------------------------------------------------===//
  // Constructors and Destructors
  //===-------------------------------------------------------------------===//

  KExtensionOptTree() : k_(1) {
    valid_ = new EdgeOffsetMap();
    new_valid_ = new EdgeOffsetMap();
    valid_list_ = new EdgeOffsetList();
    new_valid_list_ = new EdgeOffsetList();
    Sequence s;
    root_edge_ = new Edge(NULL, NULL, s);
    root_edge_->set_to(this->root_);
  }

  virtual ~KExtensionOptTree() {
    delete valid_;
    delete valid_list_;
    delete new_valid_;
    delete new_valid_list_;
    this->root_ = NULL;
  }

  //===-------------------------------------------------------------------===//
  // EditDistanceTree Interface Methods
  //===-------------------------------------------------------------------===//

  virtual void init(int new_k = 1) {
    k_ = new_k;
    row_ = 0;

    assert(this->root_);

    valid_->clear();
    valid_list_->clear();
    min_prefix_distance_ = INT_MAX;

    new_valid_->clear();
    new_valid_list_->clear();
    new_min_prefix_distance_ = INT_MAX;

    std::vector<EdgeOffset> *curr = new std::vector<EdgeOffset>();
    std::vector<EdgeOffset> *next = new std::vector<EdgeOffset>();
    curr->reserve(16);  
    next->reserve(16);  

    curr->push_back(EdgeOffset(root_edge_, 0));

    for (unsigned k=0; k < k_ && !curr->empty(); ++k) {
      for (unsigned i=0; i<curr->size(); ++i) {
        EdgeOffset &eo = (*curr)[i];
        add_valid(eo, k);
        get_children(eo, *next);
      }
      curr->clear();
      std::swap(curr, next);
    }
    delete curr;
    delete next;
  }

  virtual void add_data(Sequence &s) {
    this->insert(s);
  }

  virtual void update(Sequence &s_update) {
    Sequence suffix(s_update.begin() + row_, s_update.end());
    update_suffix(suffix);
  }

  virtual void update_suffix(Sequence &s) {
    for (unsigned i=0; i<s.size(); ++i) {
      this->update_element(s[i]);
    }
  }

  virtual void update_element(Element e) {
    std::vector<EdgeOffset> children;
    children.reserve(16);  

    for (unsigned i=0; i<valid_list_->size(); ++i) {
      EdgeOffset *eo_ptr = const_cast<EdgeOffset*>(&(*(*valid_list_)[i]));
      EdgeOffset &eo = *eo_ptr;

      int d, curr_d = eo.distance;

      if (curr_d + 1 <= k_)
        d = std::min(curr_d, add_new_valid(eo, curr_d + 1));
      else
        d = curr_d;

      if (child_count(eo) > 0) {
        children.clear();
        get_children_with_element(eo, e, children);
        for (unsigned j=0; j<children.size(); ++j)
          add_new_valid(children[j], curr_d);
      }

      if (d + 1 <= k_) {
        if (child_count(eo) > 0) {
          children.clear();
          get_children(eo, children);
          for (unsigned j=0; j<children.size(); ++j)
            add_new_valid(children[j], d + 1);
        }
      }
    }

    valid_->clear();
    valid_list_->clear();
    min_prefix_distance_ = INT_MAX;
    std::swap(valid_, new_valid_);
    std::swap(valid_list_, new_valid_list_);
    std::swap(min_prefix_distance_, new_min_prefix_distance_);
  }

  virtual int min_distance() {
    return min_prefix_distance_;
  }

  virtual int row() { return row_; }

  virtual void delete_shared_data() {
    std::stack<Node*> worklist; 
    if (this->root_) {
      worklist.push(this->root_);
      while (!worklist.empty()) {
        Node* node = worklist.top();
        EdgeMapIterator 
            it = node->edge_map().begin(), iend = node->edge_map().end();
        worklist.pop();
        for (; it != iend; ++it) {
          Edge* edge = it->second;
          worklist.push(edge->to());
          delete edge;
        }
        node->edge_map().clear();
        delete node;
      }
      delete root_edge_;
      root_edge_ = NULL;
      this->root_ = NULL;
    }
  }

  virtual EditDistanceTree<Sequence,Element>* clone_edit_distance_tree() {
    return this->clone_internal(this->root_);
  }

  //===-------------------------------------------------------------------===//
  // Overrides of virtual RadixTree methods
  //===-------------------------------------------------------------------===//

  virtual This* clone() {
    return this->clone_internal(this->clone_node(this->root_));
  }

  //===-------------------------------------------------------------------===//
  // Extra methods, testing, utility
  //===-------------------------------------------------------------------===//
  
  int lookup_edit_distance(Sequence &s) {
    Node* node = this->lookup_private(s, true);

    if (node) {
      Edge* edge = node->parent_edge();
      EdgeOffset eo(edge, edge->size()-1);
      return distance(valid_, eo); 

    } else if ((node = this->lookup_private(s, false)) != NULL) {
      Edge* edge = node->parent_edge();
      size_t offset = s.size() - (node->depth() - edge->size() + 1);
      EdgeOffset eo(edge, offset);
      return distance(valid_, eo);
    }
    return INT_MAX;
  }

  int min_prefix_distance() {
    return min_prefix_distance_;
  }

  int min_edit_distance() {
    int min_distance = INT_MAX;
    for (unsigned i=0; i<valid_list_->size(); ++i) {

      EdgeOffset &eo = *((*valid_list_)[i]);
      Edge* edge = eo.first;
      size_t offset = eo.second;
      int distance = distance(valid_, eo);

      if (edge->to()->leaf() && edge->size()-1 == offset) {
        if (min_distance > distance) {
          min_distance = distance;
        }
      }
    }
    //std::cout << "min: distance: " << min_distance 
    //  << ", value: " << min_s << std::endl;
    return min_distance;
  }

 private:

  //===-------------------------------------------------------------------===//
  // Internal Methods
  //===-------------------------------------------------------------------===//

  // Internally used constructor
  KExtensionOptTree(Node* root, unsigned k = 1) : k_(k) {
    this->root_ = root;
  }

  KExtensionOptTree* clone_internal(Node* root) {
    KExtensionOptTree *rt = new KExtensionOptTree(root);
    rt->k_ = k_;
    rt->row_ = row_;
    rt->min_prefix_distance_ = min_prefix_distance_;
    rt->new_min_prefix_distance_ = INT_MAX;
    rt->valid_ = new EdgeOffsetMap(*valid_);
    rt->valid_list_ = new EdgeOffsetList(*valid_list_);
    rt->new_valid_ = new EdgeOffsetMap();
    rt->new_valid_list_ = new EdgeOffsetList();
    rt->root_edge_ = root_edge_;
    return rt;
  }

  inline int add_valid(EdgeOffset &eo, int distance) {
    int res = add_valid_internal(valid_, valid_list_, eo, distance);
    if (min_prefix_distance_ >  distance)
      min_prefix_distance_ = distance;
    return res;
  }
  
  inline int add_new_valid(EdgeOffset &eo, int distance) {
    int res = add_valid_internal(new_valid_, new_valid_list_, eo, distance);
    if (new_min_prefix_distance_ >  distance) {
      new_min_prefix_distance_ = distance;
    }
    return res;
  }

  inline int add_valid_internal(EdgeOffsetMap* valid, EdgeOffsetList* valid_list,
                          EdgeOffset &eo, int distance) {
    EdgeOffsetMapIterator it = valid->find(eo);
    if (it != valid->end()) {
      if (it->distance > distance) {
        const_cast<EdgeOffset*>(&(*it))->distance = distance;
      } else {
        return it->distance;
      }
    } else {
      it = (*valid).insert(eo).first;
      EdgeOffset *eo_ptr = const_cast<EdgeOffset*>(&(*it));
      eo_ptr->distance = distance;
      valid_list->push_back(it);
    }
    return distance;
  }

  inline int distance(EdgeOffsetMap* valid, EdgeOffset &eo) {
    EdgeOffsetMapIterator it = valid->find(eo);
    if (it != valid->end())
      return it->distance;
    return INT_MAX;
  }

  //===-------------------------------------------------------------------===//
  // Internal EdgeOffset and helper methods
  //===-------------------------------------------------------------------===//

  struct EdgeOffset {
    EdgeOffset(Edge* _first, size_t _second) 
      : first(_first), second(_second), distance(INT_MAX) {}

    bool operator==(const EdgeOffset &other) const {
      return first == other.first && second == other.second;
    }

    Edge* first;
    size_t second;
    int distance;
  };

  struct EdgeOffsetHash : public std::unary_function<EdgeOffset, size_t> {
    size_t operator()(EdgeOffset const& eo) const {
      size_t seed = 0;
      boost::hash_combine(seed, eo.first);
      boost::hash_combine(seed, eo.second);
      return seed;
    }
  };

  inline bool has_simple_child(EdgeOffset& eo) {
    if (eo.second == 0 && eo.first->size() == 0 && !eo.first->to()->leaf())
      return false;
    if (eo.second < (eo.first->size()-1)) 
      return true;
    return false;
  }

  inline size_t child_count(EdgeOffset& eo) {
    if (eo.second == 0 && eo.first->size() == 0 && !eo.first->to()->leaf())
      return eo.first->to()->edge_map().size();
    if (eo.second < (eo.first->size()-1)) return 1;
    if (eo.first->to()->leaf()) return 0;
    return eo.first->to()->edge_map().size();
  }

  void get_children_with_element(EdgeOffset& eo, Element e, 
                                 std::vector<EdgeOffset>& children) {
    size_t n = child_count(eo);
    // Assuming we have no nodes with a single child
    if (has_simple_child(eo)) {
      EdgeIterator edge_it = eo.first->begin();
      std::advance(edge_it, eo.second + 1);
      if (*edge_it == e)
        children.push_back(EdgeOffset(eo.first, eo.second + 1));
    } else if (n > 0) {
      foreach_edge(eo.first->to(), it) {
        if (it->second->key() == e)
          children.push_back(EdgeOffset(it->second, 0));
      }
    }
  }

  void get_children(EdgeOffset& eo, std::vector<EdgeOffset>& children) {
    size_t n = child_count(eo);
    // Assuming we have no nodes with a single child
    if (has_simple_child(eo)) {
      children.push_back(EdgeOffset(eo.first, eo.second + 1));
    } else if (n > 0) {
      foreach_edge(eo.first->to(), it) {
        children.push_back(EdgeOffset(it->second, 0));
      }
    }
  }

  //===-------------------------------------------------------------------===//
  // Member variables
  //===-------------------------------------------------------------------===//

  unsigned k_;
  unsigned row_;
  int min_prefix_distance_;
  int new_min_prefix_distance_;
  EdgeOffsetMap* valid_;
  EdgeOffsetMap* new_valid_;
  EdgeOffsetList* valid_list_;
  EdgeOffsetList* new_valid_list_;
  Edge* root_edge_;
};

////////////////////////////////////////////////////////////////////////////////

template <class Sequence, class Element> 
class KExtensionTree 
: public RadixTree<Sequence, Element>, 
  public EditDistanceTree<Sequence,Element> {
 
  typedef RadixTree<Sequence, Element> This;
  typedef typename This::Node Node;
  typedef typename This::Edge Edge;
  typedef typename This::SequenceIterator EdgeIterator;
  typedef typename This::EdgeMapIterator EdgeMapIterator;
  typedef std::pair<Edge*, size_t> EdgeOffset;

  typedef boost::unordered_map<EdgeOffset, int> EdgeOffsetMap;
  //typedef google::dense_hash_map<EdgeOffset, int> EdgeOffsetMap;
  typedef typename EdgeOffsetMap::iterator EdgeOffsetMapIterator;
  typedef typename EdgeOffsetMap::value_type EdgeOffsetMapValue;
  typedef std::vector<EdgeOffset> EdgeOffsetList;

#define foreach_edge(__node, __iterator) \
  EdgeMapIterator __iterator = __node->begin(); \
  EdgeMapIterator __iterator##_END = __node->end(); \
  for (; __iterator != __iterator##_END; ++__iterator)

 public:
  //===-------------------------------------------------------------------===//
  // Constructors and Destructors
  //===-------------------------------------------------------------------===//
  
  KExtensionTree() : k_(1) {
    valid_ = new EdgeOffsetMap();
    new_valid_ = new EdgeOffsetMap();
    valid_list_ = new EdgeOffsetList();
    new_valid_list_ = new EdgeOffsetList();
    Sequence s;
    root_edge_ = new Edge(NULL, NULL, s);
    root_edge_->set_to(this->root_);
  }

  virtual ~KExtensionTree() {
    delete valid_;
    delete valid_list_;
    delete new_valid_;
    delete new_valid_list_;
    this->root_ = NULL;
  }

  //===-------------------------------------------------------------------===//
  // EditDistanceTree Interface Methods
  //===-------------------------------------------------------------------===//

  virtual void init(int new_k = 1) {
    k_ = new_k;
    row_ = 0;

    assert(this->root_);

    valid_->clear();
    valid_list_->clear();
    min_prefix_distance_ = INT_MAX;

    new_valid_->clear();
    new_valid_list_->clear();
    new_min_prefix_distance_ = INT_MAX;

    std::vector<EdgeOffset> *curr = new std::vector<EdgeOffset>();
    std::vector<EdgeOffset> *next = new std::vector<EdgeOffset>();
    curr->reserve(16);  
    next->reserve(16);  

    curr->push_back(EdgeOffset(root_edge_, 0));

    // XXX precompute for all k?
    for (unsigned k=0; k < k_ && !curr->empty(); ++k) {
      for (unsigned i=0; i<curr->size(); ++i) {
        EdgeOffset &eo = (*curr)[i];
        add_valid(eo, k);
        get_children(eo, *next);
      }
      curr->clear();
      std::swap(curr, next);
    }
    delete curr;
    delete next;
  }

  virtual void add_data(Sequence &s) {
    this->insert(s);
  }

  virtual void update(Sequence &s_update) {
    Sequence suffix(s_update.begin() + row_, s_update.end());
    update_suffix(suffix);
  }

  virtual void update_suffix(Sequence &s) {
    for (unsigned i=0; i<s.size(); ++i) {
      this->update_element(s[i]);
    }
  }

  virtual void update_element(Element e) {
    std::vector<EdgeOffset> children;
    children.reserve(16);  

    row_++;

    for (unsigned i=0; i<valid_list_->size(); ++i) {
      EdgeOffset &eo = (*valid_list_)[i];

      int curr_d = distance(valid_, eo);
      int d = curr_d;

      if (curr_d + 1 <= k_) {
        d = std::min(curr_d, add_new_valid(eo, curr_d + 1));
      }

      if (child_count(eo) > 0) {
        children.clear();
        get_children_with_element(eo, e, children);
        for (unsigned j=0; j<children.size(); ++j)
          add_new_valid(children[j], curr_d);
      }

      if (d + 1 <= k_) {
        if (child_count(eo) > 0) {
          children.clear();
          get_children(eo, children);
          for (unsigned j=0; j<children.size(); ++j)
            add_new_valid(children[j], d + 1);
        }
      }
    }

    valid_->clear();
    valid_list_->clear();
    min_prefix_distance_ = INT_MAX;
    std::swap(valid_, new_valid_);
    std::swap(valid_list_, new_valid_list_);
    std::swap(min_prefix_distance_, new_min_prefix_distance_);
  }

  virtual int min_distance() {
    return min_prefix_distance_;
  }

  virtual int row() { return row_; }

  virtual void delete_shared_data() {
    std::stack<Node*> worklist; 
    if (this->root_) {
      worklist.push(this->root_);
      while (!worklist.empty()) {
        Node* node = worklist.top();
        EdgeMapIterator 
            it = node->edge_map().begin(), iend = node->edge_map().end();
        worklist.pop();
        for (; it != iend; ++it) {
          Edge* edge = it->second;
          worklist.push(edge->to());
          delete edge;
        }
        node->edge_map().clear();
        delete node;
      }
      delete root_edge_;
      root_edge_ = NULL;
      this->root_ = NULL;
    }
  }

  virtual EditDistanceTree<Sequence,Element>* clone_edit_distance_tree() {
    return this->clone_internal(this->root_);
  }

  //===-------------------------------------------------------------------===//
  // Overrides of virtual RadixTree methods
  //===-------------------------------------------------------------------===//

  virtual This* clone() {
    return this->clone_internal(this->clone_node(this->root_));
  }

  //===-------------------------------------------------------------------===//
  // Extra methods, testing, utility
  //===-------------------------------------------------------------------===//

  int lookup_edit_distance(Sequence &s) {
    Node* node = this->lookup_private(s, true);

    if (node) {
      Edge* edge = node->parent_edge();
      EdgeOffset eo(edge, edge->size()-1);
      return distance(valid_, eo); 

    } else if ((node = this->lookup_private(s, false)) != NULL) {
      Edge* edge = node->parent_edge();
      size_t offset = s.size() - (node->depth() - edge->size() + 1);
      EdgeOffset eo(edge, offset);
      return distance(valid_, eo);
    }
    return INT_MAX;
  }

  int min_prefix_distance() {
    return min_prefix_distance_;
  }

  int min_edit_distance() {
    int min_dist = INT_MAX;
    for (unsigned i=0; i<valid_list_->size(); ++i) {

      Edge* edge = (*valid_list_)[i].first;
      size_t offset = (*valid_list_)[i].second;
      int dist = distance(valid_, (*valid_list_)[i]);

      if (edge->to()->leaf() && edge->size()-1 == offset) {
        if (min_dist > dist) {
          min_dist = dist;
        }
      }
    }
    //std::cout << "min: distance: " << min_distance 
    //  << ", value: " << min_s << std::endl;
    return min_dist;
  }

 private:

  //===-------------------------------------------------------------------===//
  // Internal Methods
  //===-------------------------------------------------------------------===//

  KExtensionTree(Node* root, unsigned k = 1) : k_(k) {
    this->root_ = root;
  }

  KExtensionTree* clone_internal(Node* root) {
    KExtensionTree *rt = new KExtensionTree(root);
    rt->k_ = k_;
    rt->row_ = row_;
    rt->min_prefix_distance_ = min_prefix_distance_;
    rt->new_min_prefix_distance_ = INT_MAX;
    rt->valid_ = new EdgeOffsetMap(*valid_);
    rt->valid_list_ = new EdgeOffsetList(*valid_list_);
    rt->new_valid_ = new EdgeOffsetMap();
    rt->new_valid_list_ = new EdgeOffsetList();
    rt->root_edge_ = root_edge_;
    return rt;
  }

  inline int add_valid(EdgeOffset &eo, int distance) {
    int res = add_valid_internal(valid_, valid_list_, eo, distance);
    if (min_prefix_distance_ >  distance)
      min_prefix_distance_ = distance;
    return res;
  }
  
  inline int add_new_valid(EdgeOffset &eo, int distance) {
    int res = add_valid_internal(new_valid_, new_valid_list_, eo, distance);
    if (new_min_prefix_distance_ >  distance) {
      //// DEBUG
      //Sequence min_s;
      //eo.first->to()->get(min_s);
      //std::cout << "KE: New min_pfx_dist: " << new_min_prefix_distance_ 
      //    << " to " << distance << ": ";
      //for (int x=0; x<min_s.size(); x++)
      //  std::cout << min_s[x];
      //std::cout << std::endl;

      new_min_prefix_distance_ = distance;
    }
    return res;
  }

  inline int add_valid_internal(EdgeOffsetMap* valid, EdgeOffsetList* valid_list,
                          EdgeOffset &eo, int distance) {
    EdgeOffsetMapIterator it = valid->find(eo);
    if (it != valid->end()) {
      if (it->second > distance)
        it->second = distance;
      else
        return it->second;
    } else {
      it = valid->insert(EdgeOffsetMapValue(eo, distance)).first;
      valid_list->push_back(eo);
    }
    return distance;
  }

  inline int distance(EdgeOffsetMap* valid, EdgeOffset &eo) {
    EdgeOffsetMapIterator it = valid->find(eo);
    if (it != valid->end())
      return it->second;
    return INT_MAX;
  }

  //===-------------------------------------------------------------------===//
  // Internal EdgeOffset helper methods
  //===-------------------------------------------------------------------===//

  //struct EdgeOffsetHash : public std::unary_function<EdgeOffset, size_t> {
  //  size_t operator()(EdgeOffset const& eo) const {
  //    size_t seed = 0;
  //    boost::hash_combine(seed, eo.first);
  //    boost::hash_combine(seed, eo.second);
  //    return seed;
  //  }
  //};

  inline bool has_simple_child(EdgeOffset& eo) {
    if (eo.second == 0 && eo.first->size() == 0 && !eo.first->to()->leaf())
      return false;
    if (eo.second < (eo.first->size()-1)) 
      return true;
    return false;
  }

  inline size_t child_count(EdgeOffset& eo) {
    if (eo.second == 0 && eo.first->size() == 0 && !eo.first->to()->leaf())
      return eo.first->to()->edge_map().size();
    if (eo.second < (eo.first->size()-1)) return 1;
    if (eo.first->to()->leaf()) return 0;
    return eo.first->to()->edge_map().size();
  }

  void get_children_with_element(EdgeOffset& eo, Element e, 
                                 std::vector<EdgeOffset>& children) {
    size_t n = child_count(eo);
    // Assuming we have no nodes with a single child
    if (has_simple_child(eo)) {

      EdgeIterator edge_it = eo.first->begin();
      std::advance(edge_it, eo.second + 1);

      if (*edge_it == e)
        children.push_back(EdgeOffset(eo.first, eo.second + 1));

    } else if (n > 0) {
      foreach_edge(eo.first->to(), it) {
        if (it->second->key() == e)
          children.push_back(EdgeOffset(it->second, 0));
      }
    }
  }

  void get_children(EdgeOffset& eo, std::vector<EdgeOffset>& children) {
    size_t n = child_count(eo);
    // Assuming we have no nodes with a single child
    if (has_simple_child(eo)) {
      children.push_back(EdgeOffset(eo.first, eo.second + 1));
    } else if (n > 0) {
      foreach_edge(eo.first->to(), it) {
        children.push_back(EdgeOffset(it->second, 0));
      }
    }
  }

  //===-------------------------------------------------------------------===//
  // Member variables
  //===-------------------------------------------------------------------===//

  unsigned k_;
  unsigned row_;
  int min_prefix_distance_;
  int new_min_prefix_distance_;
  EdgeOffsetMap* valid_;
  EdgeOffsetMap* new_valid_;
  EdgeOffsetList* valid_list_;
  EdgeOffsetList* new_valid_list_;
  Edge* root_edge_;
};

////////////////////////////////////////////////////////////////////////////////

} // end namespace cliver

////////////////////////////////////////////////////////////////////////////////

#endif // CLIVER_K_EXTENSION_TREE_H

