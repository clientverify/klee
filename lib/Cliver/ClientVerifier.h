//===-- ClientVerifier.h ----------------------------------------*- C++ -*-===//
//
// <insert license>
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//
#ifndef CLIVER_H
#define CLIVER_H

#include "CVStream.h"

#include <fstream>
#include <map>
#include <string>
#include <vector>

namespace cliver {

class CVContext {
 public:
  CVContext() {}
  int id() {return context_id_;}
 private:
  int context_id_;
};

class ClientVerifier {
 public:
  ClientVerifier();
  virtual ~ClientVerifier();
  inline CVStream* getCVStream() { return cvstream_; }
  void init();
 private:
  CVStream *cvstream_;
};

} // end namespace cliver

#endif // CLIVER_H
