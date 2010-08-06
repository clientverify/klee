#include "expr/Lexer.h"
#include "expr/Parser.h"

#include "klee/Constraints.h"
#include "klee/Expr.h"
#include "klee/ExprBuilder.h"
#include "klee/Solver.h"
#include "klee/Statistics.h"
#include "klee/util/ExprPPrinter.h"
#include "klee/util/ExprVisitor.h"

#include "llvm/ADT/OwningPtr.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/System/Signals.h"

#include "memory_update.h"
#include "constraint_set.h"
#include "path_segment.h"
#include "xpilot_checker.h"

using namespace llvm;
using namespace klee;
using namespace klee::expr;

namespace {
  llvm::cl::opt<std::string>
  InputFile(llvm::cl::desc("<input query log>"), llvm::cl::Positional,
            llvm::cl::init("-"));

  cl::opt<std::string>
  PacketConstraintDir("packet-constraint-dir", 
                      cl::desc("REQUIRED: Directory of packet constraints"),
                      cl::init(""));

  cl::opt<std::string>
  InputConstraintDir("input-constraint-dir", 
                      cl::desc("REQUIRED: Directory of input constraints"),
                      cl::init(""));

  cl::opt<std::string>
  ServerLogDir("server-log-dir", 
                      cl::desc("REQUIRED: Directory of frame logs from server"),
                      cl::init(""));

  cl::opt<std::string>
  InitialState("initial-state-file", 
                      cl::desc("Initial Client State file (.pc)"),
                      cl::init(""));
}

int main(int argc, char **argv) {
  llvm::sys::PrintStackTraceOnErrorSignal();
  llvm::cl::ParseCommandLineOptions(argc, argv);
  
  // Need at least one constraint directory and a log directory
  if ( !(InputConstraintDir.size() || PacketConstraintDir.size()) 
      || !ServerLogDir.size()) {
    llvm::cerr << "Missing REQUIRED parameter(s).\n";
    exit(0);
  }

  XPilotChecker xc(InputConstraintDir, PacketConstraintDir, ServerLogDir, InitialState);
  if (!xc.init()) {
    llvm::cerr << "XPilotChecker initialization failed\n";
  }
  xc.checkValidity();

  return 0;
 }
