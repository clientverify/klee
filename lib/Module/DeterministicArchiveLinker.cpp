//===-- DeterministicArchiveLinker.cpp  ------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "klee/Internal/Support/ModuleUtil.h"
#include "klee/Config/Version.h"
// FIXME: This does not belong here.
#include "../Core/Common.h"
#include "../Core/SpecialFunctionHandler.h"

#if LLVM_VERSION_CODE >= LLVM_VERSION(3, 3)
#error "LLVM 3.3 deterministic archive linking not implemented"
#else
#include "llvm/Function.h"
#include "llvm/LLVMContext.h"
#include "llvm/Instructions.h"
#include "llvm/IntrinsicInst.h"
#include "llvm/Module.h"
#include "llvm/Bitcode/Archive.h"
#include "llvm/ADT/SetOperations.h"
#endif

#include "llvm/Linker.h"
#include "llvm/Assembly/AssemblyAnnotationWriter.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/InstIterator.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/Support/Path.h"

#if LLVM_VERSION_CODE < LLVM_VERSION(3, 5)
#include "llvm/Support/CallSite.h"
#else
#include "llvm/IR/CallSite.h"
#endif

#include <map>
#include <set>
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>

using namespace llvm;
using namespace klee;

/// Code modified from LLVM 2.9 (LinkArchives.cpp) to be deterministic

/// GetAllUndefinedSymbols - calculates the set of undefined symbols that still
/// exist in an LLVM module. This is a bit tricky because there may be two
/// symbols with the same name but different LLVM types that will be resolved to
/// each other but aren't currently (thus we need to treat it as resolved).
///
/// Inputs:
///  M - The module in which to find undefined symbols.
///
/// Outputs:
///  UndefinedSymbols - A set of C++ strings containing the name of all
///                     undefined symbols.
///
static void
GetAllUndefinedSymbols(Module *M, std::set<std::string> &UndefinedSymbols) {
  std::set<std::string> DefinedSymbols;
  UndefinedSymbols.clear();

  // If the program doesn't define a main, try pulling one in from a .a file.
  // This is needed for programs where the main function is defined in an
  // archive, such f2c'd programs.
  Function *Main = M->getFunction("main");
  if (Main == 0 || Main->isDeclaration())
    UndefinedSymbols.insert("main");

  for (Module::iterator I = M->begin(), E = M->end(); I != E; ++I)
    if (I->hasName()) {
      if (I->isDeclaration())
        UndefinedSymbols.insert(I->getName());
      else if (!I->hasLocalLinkage()) {
        assert(!I->hasDLLImportLinkage()
               && "Found dllimported non-external symbol!");
        DefinedSymbols.insert(I->getName());
      }      
    }

  for (Module::global_iterator I = M->global_begin(), E = M->global_end();
       I != E; ++I)
    if (I->hasName()) {
      if (I->isDeclaration())
        UndefinedSymbols.insert(I->getName());
      else if (!I->hasLocalLinkage()) {
        assert(!I->hasDLLImportLinkage()
               && "Found dllimported non-external symbol!");
        DefinedSymbols.insert(I->getName());
      }      
    }

  for (Module::alias_iterator I = M->alias_begin(), E = M->alias_end();
       I != E; ++I)
    if (I->hasName())
      DefinedSymbols.insert(I->getName());

  // Prune out any defined symbols from the undefined symbols set...
  for (std::set<std::string>::iterator I = UndefinedSymbols.begin();
       I != UndefinedSymbols.end(); )
    if (DefinedSymbols.count(*I))
      UndefinedSymbols.erase(I++);  // This symbol really is defined!
    else
      ++I; // Keep this symbol in the undefined symbols list
}

Module *klee::deterministicLinkWithLibrary(Module *module, 
                                const std::string &libraryName) {
  Linker linker("klee", module, false);
  llvm::sys::Path Filename(libraryName);
  bool native = false;

  std::vector<Module*> archiveModules;
  std::set<std::string> UndefinedSymbols;
  GetAllUndefinedSymbols(module, UndefinedSymbols);

  if (UndefinedSymbols.size() == 0) {
    klee_message("No symbols undefined, skipping library '%s'", 
                 libraryName.c_str());
    return linker.releaseModule();
  }

  std::string ErrMsg;
  std::auto_ptr<Archive> AutoArch (
    Archive::OpenAndLoadSymbols(Filename, getGlobalContext(), &ErrMsg));

  Archive* arch = AutoArch.get();

  if (!arch) {
    klee_error("Cannot read archive %s", libraryName.c_str());
  }

  // Save a set of symbols that are not defined by the archive. Since we're
  // entering a loop, there's no point searching for these multiple times. This
  // variable is used to "set_subtract" from the set of undefined symbols.
  std::set<std::string> NotDefinedByArchive;

  // Save the current set of undefined symbols, because we may have to make
  // multiple passes over the archive:
  std::set<std::string> CurrentlyUndefinedSymbols;

  do {
    CurrentlyUndefinedSymbols = UndefinedSymbols;

    // Find the modules we need to link into the target module.  Note that arch
    // keeps ownership of these modules and may return the same Module* from a
    // subsequent call.
    std::set<Module*> Modules;
    if (!arch->findModulesDefiningSymbols(UndefinedSymbols, Modules, &ErrMsg)) {
      klee_error("Cannot find symbols in %s %s", Filename.str().c_str(), ErrMsg.c_str());
    }

    // If we didn't find any more modules to link this time, we are done
    // searching this archive.
    if (Modules.empty())
      break;

    // Any symbols remaining in UndefinedSymbols after
    // findModulesDefiningSymbols are ones that the archive does not define. So
    // we add them to the NotDefinedByArchive variable now.
    NotDefinedByArchive.insert(UndefinedSymbols.begin(),
        UndefinedSymbols.end());

    // Sort by module name so for determinisitic loading
    std::vector<std::string> moduleIdentifiers;
    std::map<std::string, Module*> moduleIdentifierMap;
    for (std::set<Module*>::iterator I=Modules.begin(), E=Modules.end(); I != E; ++I) {
      Module* aModule = *I;
      assert(aModule != NULL);
      std::string aModuleID = aModule->getModuleIdentifier();
      assert(moduleIdentifierMap.count(aModuleID) == 0);
      moduleIdentifiers.push_back(aModuleID);
      moduleIdentifierMap[aModuleID] = aModule;
    }
    std::sort(moduleIdentifiers.begin(), moduleIdentifiers.end());

    // Loop over all the Modules that we got back from the archive
    for (std::vector<std::string>::iterator I=moduleIdentifiers.begin(), 
         E=moduleIdentifiers.end(); I != E; ++I) {

      // Get the module we must link in.
      std::string moduleErrorMsg;
      //Module* aModule = *I;
      Module* aModule = moduleIdentifierMap[*I];
      if (aModule != NULL) {
        if (aModule->MaterializeAll(&moduleErrorMsg)) {
          klee_error("Could not load a module: %s", moduleErrorMsg.c_str());
        }

        //klee_message("  Linking in module: %s", aModule->getModuleIdentifier().c_str());

        // Link it in
        if (linker.LinkInModule(aModule, &moduleErrorMsg)) {
          klee_error("Cannot link in module '%s': %s",
                     aModule->getModuleIdentifier().c_str(), moduleErrorMsg.c_str());
        }
      } 
    }
    
    // Get the undefined symbols from the aggregate module. This recomputes the
    // symbols we still need after the new modules have been linked in.
    GetAllUndefinedSymbols(module, UndefinedSymbols);

    // At this point we have two sets of undefined symbols: UndefinedSymbols
    // which holds the undefined symbols from all the modules, and
    // NotDefinedByArchive which holds symbols we know the archive doesn't
    // define. There's no point searching for symbols that we won't find in the
    // archive so we subtract these sets.
    set_subtract(UndefinedSymbols, NotDefinedByArchive);

    // If there's no symbols left, no point in continuing to search the
    // archive.
    if (UndefinedSymbols.empty())
      break;
  } while (CurrentlyUndefinedSymbols != UndefinedSymbols);

  return linker.releaseModule();
}

