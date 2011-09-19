//===-- CVCommon.h ----------------------------------------------*- C++ -*-===//
//
// <insert license>
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//
#ifndef CLIVER_COMMON_H
#define CLIVER_COMMON_H

// Boost FOREACH macro
#include <boost/foreach.hpp>
#define foreach BOOST_FOREACH 
#define reverse_foreach BOOST_REVERSE_FOREACH 

// STL Containers
#include <deque>
#include <set>
#include <map>
#include <string>
#include <vector>

// http://www.parashift.com/c++-faq-lite/misc-technical-issues.html#faq-39.6
#define CONCAT_TOKEN_(foo, bar) CONCAT_TOKEN_IMPL_(foo, bar)
#define CONCAT_TOKEN_IMPL_(foo, bar) foo ## bar

#include "llvm/Support/CommandLine.h"

#include "CVStream.h"

#endif // CLIVER_COMMON_H
