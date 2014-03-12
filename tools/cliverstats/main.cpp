//===-- tools/cliver/main.cpp -----------------------------------*- C++ -*-===//
//
// <insert license>
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//

#include <stdlib.h>

#if (LLVM_VERSION_MAJOR == 2 && LLVM_VERSION_MINOR < 7)
#error
#endif

#include "../../lib/Core/Common.h"
#include "../../lib/Cliver/CVCommon.h"
#include "cliver/ClientVerifier.h"
#include "cliver/CVExecutor.h"
#include "cliver/CVStream.h"
#include "cliver/Training.h"

#include "klee/Config/config.h"
#include "klee/ExecutionState.h"
#include "klee/Expr.h"
#include "klee/Internal/ADT/KTest.h"
#include "klee/Internal/ADT/TreeStream.h"
#include "klee/Internal/Support/ModuleUtil.h"
#include "klee/Internal/System/Time.h"
#include "klee/Interpreter.h"
#include "klee/Statistics.h"

#include "llvm/Bitcode/ReaderWriter.h"
#include "llvm/Constants.h"
#include "llvm/InstrTypes.h"
#include "llvm/Instruction.h"
#include "llvm/Instructions.h"
#include "llvm/LLVMContext.h"
#include "llvm/Module.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Type.h"

// FIXME: Ugh, this is gross. But otherwise our config.h conflicts with LLVMs.
#undef PACKAGE_BUGREPORT
#undef PACKAGE_NAME
#undef PACKAGE_STRING
#undef PACKAGE_TARNAME
#undef PACKAGE_VERSION
#if LLVM_VERSION_CODE < LLVM_VERSION(3, 0)
#include "llvm/Target/TargetSelect.h"
#else
#include "llvm/Support/TargetSelect.h"
#endif
#include "llvm/Support/Signals.h"
#include "llvm/Support/system_error.h"


#include <cerrno>
#include <dirent.h>
#include <errno.h>
#include <fstream>
#include <iostream>
#include <iterator>
#include <signal.h>
#include <sstream>
#include <sys/stat.h>
#include <sys/wait.h>

namespace {

enum StatsModeType { PathStats };
llvm::cl::opt<StatsModeType>
StatsMode("mode", 
    llvm::cl::desc("Select mode:"),
    llvm::cl::values(
      clEnumValN(PathStats, "path", "Path stats"),
      clEnumValEnd),
    llvm::cl::init(PathStats));

llvm::cl::list<std::string> InputFiles("input",
    llvm::cl::ZeroOrMore,
    llvm::cl::ValueRequired,
    llvm::cl::desc("Specify a training path file (.tpath)"), 
    llvm::cl::value_desc("tpath file"));

llvm::cl::list<std::string> InputDir("input-dir",
    llvm::cl::ZeroOrMore,
    llvm::cl::ValueRequired,
    llvm::cl::desc("Specify directory containing .tpath files"),
    llvm::cl::value_desc("tpath directory"));
}

cliver::CVStream *g_cvstream;

void DoPathStats()
{
  using namespace cliver;

  std::set<TrainingObject*> training_objects;

  if (!InputDir.empty()) {
    foreach (std::string path, InputDir) {
      g_cvstream->getFilesRecursive(path, ".tpath", InputFiles);
    }
  }

  CVMESSAGE("Reading tpath files");
  TrainingManager::read_files(InputFiles, training_objects); 

  CVMESSAGE("Successfully read " << training_objects.size() << " files.");

  foreach (TrainingObject *tobj, training_objects)
  {
    CVMESSAGE(*tobj);
  }
}

//===----------------------------------------------------------------------===//
// main
//===----------------------------------------------------------------------===//
int main(int argc, char **argv, char **envp) {
  using namespace llvm;

  llvm::cl::ParseCommandLineOptions(argc, argv, " cliverstats\n");

  bool no_output = true;
  std::string output_dir("./cliverstats-out");
  g_cvstream = new cliver::CVStream(no_output, output_dir);
  g_cvstream->init();

  switch (StatsMode)
  {
    case PathStats: 
      {
        DoPathStats();
        break;
      }
  }

  return 0;
}

