/* NUKLEAR KLEE begin (ENTIRE FILE) */

#ifndef KLEE_NUKLEARSOCKET_H
#define KLEE_NUKLEARSOCKET_H

#include "klee/Internal/ADT/KTest.h"

// TODO : fix these comments
// Each ExecutionState will have a NuklearObjectState object.
// position holds the most recently read object from KTest.
// position may vary amongst ExecutionStates until each state reaches a
// klee_socket_write() where states will wait to merge.
// klee_socket_read() and klee_socket_send() will check the name of each object
// to ensure klee_socket_write() and klee_socket_read() correct order.

namespace klee {

class NuklearSocket {
public:
  NuklearSocket() 
    : index(0), bytes(0), ktest(NULL) {}
  NuklearSocket(const struct KTest *_ktest) 
    : index(0), bytes(0), ktest(_ktest) {}
  int index;
  int bytes;
  const struct KTest *ktest;
  int compare(const NuklearSocket &b) {
    return !(index == b.index && bytes == b.bytes && ktest == b.ktest);
  }
};

} // end namespace klee

#endif //KLEE_NUKLEARSOCKET_H
/* NUKLEAR KLEE end (ENTIRE FILE) */
