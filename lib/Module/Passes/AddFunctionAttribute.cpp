//===-- AddFunctionAttribute.cpp ------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "../Passes.h"
#include "AddFunctionAttribute.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/raw_ostream.h"

#include <iterator>
#include <string>
#include <fstream>
#include <sstream>
#include <iostream>
#include <vector>
#include <algorithm>

llvm::cl::opt<std::string>
AddFunctionAttributeInputFile("addfuncattrfile",
                              llvm::cl::init(""));

using namespace llvm;

char klee::AddFunctionAttribute::ID = 0;
static llvm::RegisterPass<klee::AddFunctionAttribute> RP_addfuncattr(
    "addfuncattr", "Add Function Attributes (klee)", false, false);

namespace klee {

static std::vector<std::string> split(const std::string &s, char delim) {
  std::stringstream ss(s);
  std::string item;
  std::vector<std::string> elems;
  while (std::getline(ss, item, delim)) {
    elems.push_back(item);
  }
  return elems;
}

static Attribute::AttrKind getAttrKindFromString(std::string s) {

  if (s ==  "sanitize_address")
    return Attribute::SanitizeAddress;
  if (s ==  "alwaysinline")
    return Attribute::AlwaysInline;
  if (s ==  "builtin")
    return Attribute::Builtin;
  if (s ==  "byval")
    return Attribute::ByVal;
  if (s ==  "inlinehint")
    return Attribute::InlineHint;
  if (s ==  "inreg")
    return Attribute::InReg;
  if (s ==  "minsize")
    return Attribute::MinSize;
  if (s ==  "naked")
    return Attribute::Naked;
  if (s ==  "nest")
    return Attribute::Nest;
  if (s ==  "noalias")
    return Attribute::NoAlias;
  if (s ==  "nobuiltin")
    return Attribute::NoBuiltin;
  if (s ==  "nocapture")
    return Attribute::NoCapture;
  if (s ==  "noduplicate")
    return Attribute::NoDuplicate;
  if (s ==  "noimplicitfloat")
    return Attribute::NoImplicitFloat;
  if (s ==  "noinline")
    return Attribute::NoInline;
  if (s ==  "nonlazybind")
    return Attribute::NonLazyBind;
  if (s ==  "noredzone")
    return Attribute::NoRedZone;
  if (s ==  "noreturn")
    return Attribute::NoReturn;
  if (s ==  "nounwind")
    return Attribute::NoUnwind;
  if (s ==  "optnone")
    return Attribute::OptimizeNone;
  if (s ==  "optsize")
    return Attribute::OptimizeForSize;
  if (s ==  "readnone")
    return Attribute::ReadNone;
  if (s ==  "readonly")
    return Attribute::ReadOnly;
  if (s ==  "returned")
    return Attribute::Returned;
  if (s ==  "returns_twice")
    return Attribute::ReturnsTwice;
  if (s ==  "signext")
    return Attribute::SExt;
  if (s ==  "ssp")
    return Attribute::StackProtect;
  if (s ==  "sspreq")
    return Attribute::StackProtectReq;
  if (s ==  "sspstrong")
    return Attribute::StackProtectStrong;
  if (s ==  "sret")
    return Attribute::StructRet;
  if (s ==  "sanitize_thread")
    return Attribute::SanitizeThread;
  if (s ==  "sanitize_memory")
    return Attribute::SanitizeMemory;
  if (s ==  "uwtable")
    return Attribute::UWTable;
  if (s ==  "zeroext")
    return Attribute::ZExt;
  if (s ==  "cold")
    return Attribute::Cold;

  return Attribute::None;
}

AddFunctionAttribute::AddFunctionAttribute() : llvm::ModulePass(ID) {
  
  if (AddFunctionAttributeInputFile != "") {
    std::ifstream inputfile  { AddFunctionAttributeInputFile };

    std::string line;
    while (std::getline(inputfile, line)) {
      std::vector<std::string> words;
      for (auto s : split(line, ' ')) {
        if (s == "#") break;
        words.push_back(s);
      }
      if (words.size() >= 2) {
        std::vector<llvm::Attribute> attrs;
        auto it=words.begin(), ie=words.end();
        std::string funcName = *it;
        ++it;
        for (; it!=ie; ++it) {
          Attribute::AttrKind kind = getAttrKindFromString(*it);
          if (kind != Attribute::None)
            attrs.push_back(Attribute::get(getGlobalContext(),kind));
        }
        if (attrs.size())
          Attributes[funcName] = attrs;
      }
    }
    for (auto x : Attributes) {
      llvm::errs() << "Function: " << x.first << " Attributes: ";
      for (auto attr : x.second) {
        llvm::errs()  
            << "(" << attr.getAsString() << ")"
      }
      llvm::errs() << "\n";
    }
  }
}

bool AddFunctionAttribute::runOnModule(llvm::Module &M) {
  bool dirty = false;
  for (llvm::Module::iterator f=M.begin(), fe=M.end(); f!=fe; ++f) {
    llvm::Function &F = *f;
    if (Attributes.count(F.getName().str()) > 0) {
      F.addAttribute(AttributeSet::FunctionIndex, Attribute::NoInline);
      dirty = true;
    }
  }
  return dirty;
}

} // end namespace klee

