//===-- tools/cliverstats/main.cpp ------------------------------*- C++ -*-===//
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

#if LLVM_VERSION_CODE > LLVM_VERSION(3, 2)
#include "llvm/IR/Constants.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LLVMContext.h"
#else
#include "llvm/Constants.h"
#include "llvm/Module.h"
#include "llvm/Type.h"
#include "llvm/InstrTypes.h"
#include "llvm/Instruction.h"
#include "llvm/Instructions.h"
#include "llvm/LLVMContext.h"
#include "llvm/Support/FileSystem.h"
#endif
#include "llvm/Support/FileSystem.h"
#include "llvm/Bitcode/ReaderWriter.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/raw_ostream.h"

#if LLVM_VERSION_CODE < LLVM_VERSION(3, 0)
#include "llvm/Target/TargetSelect.h"
#else
#include "llvm/Support/TargetSelect.h"
#endif
#include "llvm/Support/Signals.h"

#if LLVM_VERSION_CODE < LLVM_VERSION(3, 5)
#include "llvm/Support/system_error.h"
#endif

#include <dirent.h>
#include <signal.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include <cerrno>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <sstream>

namespace {

enum StatsModeType { PathStats, HMMTest };
llvm::cl::opt<StatsModeType>
StatsMode("mode", 
    llvm::cl::desc("Select mode:"),
    llvm::cl::values(
      clEnumValN(PathStats, "path", "Path stats"),
      clEnumValN(HMMTest, "hmmtest", "HMM test"),
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
    CVMESSAGE("(" << tobj->name << ") " << tobj->trace);
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
    case HMMTest:
      {
        // Do nothing - we removed ghmm
        break;
      }

  }

  return 0;

}

