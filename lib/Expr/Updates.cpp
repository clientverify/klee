//===-- Updates.cpp -------------------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "klee/Expr.h"

#include <cassert>

using namespace klee;

///

UpdateNode::UpdateNode(const UpdateNode *_next, 
                       const ref<Expr> &_index, 
                       const ref<Expr> &_value) 
  : refCount(0),
    stpArray(0),
    next(_next),
    index(_index),
    value(_value) {
  assert(_value->getWidth() == Expr::Int8 && 
         "Update value should be 8-bit wide.");
  computeHash();
  if (next) {
    ++next->refCount;
    size = 1 + next->size;
  }
  else size = 1;
}

extern "C" void vc_DeleteExpr(void*);

UpdateNode::~UpdateNode() {
  // XXX gross
  if (stpArray)
    ::vc_DeleteExpr(stpArray);
}

int UpdateNode::compare(const UpdateNode &b) const {
  if (int i = index.compare(b.index)) 
    return i;
  return value.compare(b.value);
}

unsigned UpdateNode::computeHash() {
  hashValue = index->hash() ^ value->hash();
  if (next)
    hashValue ^= next->hash();
  return hashValue;
}

///

UpdateList::UpdateList(const Array *_root, const UpdateNode *_head)
  : root(_root),
    head(_head) {
  if (head) ++head->refCount;
}

UpdateList::UpdateList(const UpdateList &b)
  : root(b.root),
    head(b.head) {
  if (head) ++head->refCount;
}

UpdateList::~UpdateList() {
  // We need to be careful and avoid recursion here. We do this in
  // cooperation with the private dtor of UpdateNode which does not
  // recursively free its tail.
  while (head && --head->refCount==0) {
    const UpdateNode *n = head->next;
    delete head;
    head = n;
  }
}

UpdateList &UpdateList::operator=(const UpdateList &b) {
  if (b.head) ++b.head->refCount;
  if (head && --head->refCount==0) delete head;
  root = b.root;
  head = b.head;
  return *this;
}

void UpdateList::extend(const ref<Expr> &index, const ref<Expr> &value) {
  if (head) --head->refCount;
  head = new UpdateNode(head, index, value);
  ++head->refCount;
}

int UpdateList::compare(const UpdateList &b) const {
  if (root->name != b.root->name)
    return root->name < b.root->name ? -1 : 1;

  // Check the root itself in case we have separate objects with the
  // same name.
  if (root != b.root)
    return root < b.root ? -1 : 1;

  if (getSize() < b.getSize()) return -1;
  else if (getSize() > b.getSize()) return 1;    

  // XXX build comparison into update, make fast
  const UpdateNode *an=head, *bn=b.head;
  for (; an && bn; an=an->next,bn=bn->next) {
    if (an==bn) { // exploit shared list structure
      return 0;
    } else {
      if (int res = an->compare(*bn))
        return res;
    }
  }
  assert(!an && !bn);  
  return 0;
}

/* NUKLEAR KLEE begin */
void UpdateList::computeDigest(EVP_MD_CTX *mdctx) {
  if (root && root->name.size())
    EVP_DigestUpdate(mdctx, root->name.c_str(), root->name.size());

  // Check the root itself in case we have separate objects with the
  // same name.
  EVP_DigestUpdate(mdctx, &root, sizeof(root));

  unsigned get_size = getSize();
  EVP_DigestUpdate(mdctx, &get_size, sizeof(get_size));

  //unsigned myhash = hash();
  //EVP_DigestUpdate(mdctx, &myhash, sizeof(myhash));

  // XXX build comparison into update, make fast
  const UpdateNode *an=head;
  //std::stringstream ss;
  for (; an ; an=an->next) {
    EVP_DigestUpdate(mdctx, &an, sizeof(an));
    unsigned hash_value = an->value->hash();
    unsigned hash_index = an->index->hash();
    EVP_DigestUpdate(mdctx, &hash_value, sizeof(hash_value));
    EVP_DigestUpdate(mdctx, &hash_index, sizeof(hash_index));
    //ss << an->value;
    //ss << an->index;
  }
  //if (ss.str().size()) 
  //  EVP_DigestUpdate(mdctx, ss.str().c_str(), ss.str().size());
}
/* NUKLEAR KLEE end*/

unsigned UpdateList::hash() const {
  unsigned res = 0;
  for (unsigned i = 0, e = root->name.size(); i != e; ++i)
    res = (res * Expr::MAGIC_HASH_CONSTANT) + root->name[i];
  if (head)
    res ^= head->hash();
  return res;
}
