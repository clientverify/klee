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

class CVExecutor;

class CVContext {
 public:
  CVContext();
  int id() {return context_id_;}
 private:
  int increment_id() { return next_id_++; }

  int context_id_;
  static int next_id_;
};

class ClientVerifier {
 public:
  ClientVerifier();
  virtual ~ClientVerifier();
  inline CVStream* getCVStream() { return cvstream_; }
  void init();
	void prepare_to_run(CVExecutor *executor);
 private:
  CVStream *cvstream_;
};

} // end namespace cliver

#endif // CLIVER_H
