/*
 * Cloud9 Parallel Symbolic Execution Engine
 *
 * Copyright (c) 2011, Dependable Systems Laboratory, EPFL
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of the Dependable Systems Laboratory, EPFL nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE DEPENDABLE SYSTEMS LABORATORY, EPFL BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * All contributors are listed in CLOUD9-AUTHORS file.
 *
*/

#include "Common.h"

#include "klee/Cloud9Init.h"
#include "klee/Internal/Support/ModuleUtil.h"
#include "klee/Config/Version.h"

#include "llvm/Support/CommandLine.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Signals.h"
#include "llvm/Support/system_error.h"
#include "llvm/Bitcode/ReaderWriter.h"

#if LLVM_VERSION_CODE >= LLVM_VERSION(3, 3)
#include "llvm/IR/Constants.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/ValueSymbolTable.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Type.h"
#else
#include "llvm/Constants.h"
#include "llvm/Instruction.h"
#include "llvm/Instructions.h"
#include "llvm/InstrTypes.h"
#include "llvm/LLVMContext.h"
#include "llvm/Module.h"
#include "llvm/Type.h"
#include "llvm/ValueSymbolTable.h"
#if LLVM_VERSION_CODE <= LLVM_VERSION(3, 1)
#include "llvm/Target/TargetData.h"
#else
#include "llvm/DataLayout.h"
#endif
#endif

#include <iostream>
#include <map>
#include <set>
#include <fstream>


using namespace llvm;
using namespace klee;

namespace {
  cl::opt<bool>
  DebugModelPatches("debug-model-patches",
                   cl::desc("Shows information about the patching of modeled library functions."));

  cl::list<std::string>
  DisabledFunctionModels("disable-fn-model",
    cl::ZeroOrMore,
    cl::ValueRequired,
    cl::desc("Disable modeled functions"));


}

namespace klee {

static llvm::Module *linkWithPOSIX(llvm::Module *mainModule, const std::string &Path) {
  Function *mainFn = mainModule->getFunction("main");
  mainModule->getOrInsertFunction("__force_model_linkage",
      Type::getVoidTy(getGlobalContext()), NULL);
  mainModule->getOrInsertFunction("klee_init_env",
      Type::getVoidTy(getGlobalContext()),
      PointerType::getUnqual(mainFn->getFunctionType()->getParamType(0)),
      PointerType::getUnqual(mainFn->getFunctionType()->getParamType(1)),
      NULL);

  mainModule->getOrInsertFunction("_exit",
      Type::getVoidTy(getGlobalContext()),
      Type::getInt32Ty(getGlobalContext()), NULL);

  klee_message("NOTE: Using model: %s", Path.c_str());
  mainModule = klee::linkWithLibrary(mainModule, Path.c_str());
  assert(mainModule && "unable to link with simple model");

  std::map<std::string, const GlobalValue*> underlyingFn;

  for (Module::iterator it = mainModule->begin(); it != mainModule->end(); it++) {
    Function *modelFunction = it;
    StringRef modelName = modelFunction->getName();

    if (!modelName.startswith("__klee_model_"))
      continue;

    StringRef modelledName = modelName.substr(strlen("__klee_model_"), modelName.size());

    bool modelDisabled = false;
    for (auto disabledFnModel : DisabledFunctionModels) {
      if (modelledName == disabledFnModel) {
        modelDisabled = true;
        break;
      }
    }
    if (modelDisabled) {
      if (DebugModelPatches) {
        klee_message("Disabled patching %s", modelName.data());
      }
      continue;
    }

    const GlobalValue *modelledFunction = mainModule->getNamedValue(modelledName);

    if (modelledFunction != NULL) {
      if (const GlobalAlias *modelledA = dyn_cast<GlobalAlias>(modelledFunction)) {
        const GlobalValue *GV = modelledA->resolveAliasedGlobal(false);
        if (!GV || GV->getType() != modelledFunction->getType())
          continue; // TODO: support bitcasted aliases
        modelledFunction = GV;
      }

      underlyingFn[modelledName.str()] = modelledFunction;

      if (DebugModelPatches) {
        klee_message("Patching %s", modelName.data());
        //std::cerr << "Modelled: "; modelledFunction->getType()->dump(); std::cerr << std::endl;
        //std::cerr << "Model:    "; modelFunction->getType()->dump(); std::cerr << std::endl;
      }

      Constant *adaptedFunction = mainModule->getOrInsertFunction(
          modelFunction->getName(), dyn_cast<Function>(modelledFunction)->getFunctionType());

      const_cast<GlobalValue*>(modelledFunction)->replaceAllUsesWith(adaptedFunction);
    }
  }

  for (Module::iterator it = mainModule->begin(); it != mainModule->end();) {
    Function *originalFunction = it;
    StringRef originalName = originalFunction->getName();

    if (!originalName.startswith("__klee_original_")) {
      it++;
      continue;
    }

    StringRef targetName = originalName.substr(strlen("__klee_original_"));

    if (DebugModelPatches) {
      klee_message("Patching %s", originalName.data());
    }

    const GlobalValue *targetFunction;
    if (underlyingFn.count(targetName.str()) > 0)
      targetFunction = underlyingFn[targetName.str()];
    else
      targetFunction = mainModule->getNamedValue(targetName);

    if (targetFunction) {
      Constant *adaptedFunction = mainModule->getOrInsertFunction(
          targetFunction->getName(), dyn_cast<Function>(originalFunction)->getFunctionType());

      originalFunction->replaceAllUsesWith(adaptedFunction);
      it++;
      originalFunction->eraseFromParent();
    } else {
      // We switch to strings in order to avoid memory errors due to StringRef
      // destruction inside setName().
      std::string targetNameStr = targetName.str();
      originalFunction->setName(targetNameStr);
      assert(originalFunction->getName().str() == targetNameStr);
      it++;
    }

  }

  return mainModule;
}

static int patchMain(Module *mainModule) {
  /*
    nArgcP = alloc oldArgc->getType()
    nArgvV = alloc oldArgv->getType()
    store oldArgc nArgcP
    store oldArgv nArgvP
    klee_init_environment(nArgcP, nArgvP)
    nArgc = load nArgcP
    nArgv = load nArgvP
    oldArgc->replaceAllUsesWith(nArgc)
    oldArgv->replaceAllUsesWith(nArgv)
  */

  Function *mainFn = mainModule->getFunction("__user_main");

  if (mainFn->arg_size() < 2) {
    std::cerr << "Cannot handle ""-init-env"" when main() has less than two arguments.\n";
    return -1;
  }

  Instruction* firstInst = mainFn->begin()->begin();

  Value* oldArgc = mainFn->arg_begin();
  Value* oldArgv = ++mainFn->arg_begin();

  AllocaInst* argcPtr =
    new AllocaInst(oldArgc->getType(), "argcPtr", firstInst);
  AllocaInst* argvPtr =
    new AllocaInst(oldArgv->getType(), "argvPtr", firstInst);

  /* Insert void klee_init_env(int* argc, char*** argv) */
  std::vector<const Type*> params;
  params.push_back(Type::getInt32Ty(getGlobalContext()));
  params.push_back(Type::getInt32Ty(getGlobalContext()));
  Function* procArgsFn = mainModule->getFunction("klee_process_args");
  assert(procArgsFn);
  std::vector<Value*> args;
  args.push_back(argcPtr);
  args.push_back(argvPtr);
#if LLVM_VERSION_CODE >= LLVM_VERSION(3, 0)
  Instruction* procArgsCall = CallInst::Create(procArgsFn, args, "", firstInst);
#else
  Instruction* procArgsCall = CallInst::Create(procArgsFn, args.begin(), args.end(), 
					      "", firstInst);
#endif
  Value *argc = new LoadInst(argcPtr, "newArgc", firstInst);
  Value *argv = new LoadInst(argvPtr, "newArgv", firstInst);

  oldArgc->replaceAllUsesWith(argc);
  oldArgv->replaceAllUsesWith(argv);

  new StoreInst(oldArgc, argcPtr, procArgsCall);
  new StoreInst(oldArgv, argvPtr, procArgsCall);

  return 0;
}

static int patchLibcMain(Module *mainModule) {
  Function *libcMainFn = mainModule->getFunction("__uClibc_main");

  Instruction* firstInst = libcMainFn->begin()->begin();

  Value* argc = ++libcMainFn->arg_begin();
  Value* argv = ++(++libcMainFn->arg_begin());

  Function* initEnvFn = mainModule->getFunction("klee_init_env");
  assert(initEnvFn);
  std::vector<Value*> args;
  args.push_back(argc);
  args.push_back(argv);
#if LLVM_VERSION_CODE >= LLVM_VERSION(3, 0)
  CallInst::Create(initEnvFn, args, "", firstInst);
#else
  CallInst::Create(initEnvFn, args.begin(), args.end(), "", firstInst);
#endif

  return 0;
}

llvm::Module* linkWithCloud9POSIX(llvm::Module* module, const std::string &Path) {
  llvm::Module* mainModule = linkWithPOSIX(module, Path);

  if (mainModule) {
    if (patchMain(mainModule) != 0)
      return NULL;

    if (patchLibcMain(mainModule) != 0)
      return NULL;
  }
  return mainModule;
}

} // end namespace klee

