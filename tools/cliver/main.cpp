//===-- tools/cliver/main.cpp -----------------------------------*- C++ -*-===//
//
// <insert license>
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//

#if (LLVM_VERSION_MAJOR == 2 && LLVM_VERSION_MINOR < 7)
#error
#endif

#include "../lib/Core/Common.h"
#include "../lib/Cliver/ClientVerifier.h"
#include "../lib/Cliver/CVExecutor.h"
#include "../lib/Cliver/CVStream.h"

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
#include "llvm/Target/TargetSelect.h"
#include "llvm/System/Signals.h"

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

klee::Interpreter *g_interpreter = 0;
static bool g_interrupted = false;

namespace {
llvm::cl::opt<std::string>
InputFile(
    llvm::cl::desc("<input bytecode>"), 
    llvm::cl::Positional, 
    llvm::cl::init("-"));

llvm::cl::opt<std::string>
RunInDir("run-in", 
    llvm::cl::desc("Change to the given directory prior to executing"));

llvm::cl::opt<std::string>
Environ("environ", 
  llvm::cl::desc("Parse environ from given file (in \"env\" format)"));

llvm::cl::list<std::string>
InputArgv(
    llvm::cl::ConsumeAfter, 
    llvm::cl::desc("<program arguments>..."));

llvm::cl::opt<bool>
OnlyErrorOutput("only-error-output", 
    llvm::cl::desc("Only generate test files for errors."));

llvm::cl::opt<bool>
WarnAllExternals("warn-all-externals", 
    llvm::cl::desc("Give initial warning for all externals."));

llvm::cl::opt<bool>
ExitOnError("exit-on-error", 
    llvm::cl::desc("Exit if errors occur"));

llvm::cl::opt<bool>
WithCliverRuntime("cliver-runtime", 
    llvm::cl::desc("Link with Cliver runtime"),
    llvm::cl::init(false));

llvm::cl::opt<bool>
WithPOSIXRuntime("posix-runtime", 
    llvm::cl::desc("Link with POSIX runtime"),
    llvm::cl::init(false));

enum LibcType { NoLibc, KleeLibc, UcLibc };

llvm::cl::opt<LibcType>
Libc("libc", 
    llvm::cl::desc("Choose libc version (none by default)."),
    llvm::cl::values(
      clEnumValN(NoLibc, "none", "Don't link in a libc"),
      clEnumValN(KleeLibc, "klee", "Link in klee libc"),
      clEnumValN(UcLibc, "uclibc", "Link in uclibc (adapted for klee)"),
      clEnumValEnd),
    llvm::cl::init(NoLibc));
}

static llvm::Module *linkWithUclibc(llvm::Module *mainModule) {
	using namespace llvm;
	using namespace klee;
  Function *f;
  // force import of __uClibc_main
  mainModule->getOrInsertFunction("__uClibc_main",
                                  FunctionType::get(Type::getVoidTy(getGlobalContext()),
                                                    std::vector<const Type*>(),
                                                    true));
  
  // force various imports
  if (WithPOSIXRuntime) {
    const llvm::Type *i8Ty = Type::getInt8Ty(getGlobalContext());
    mainModule->getOrInsertFunction("realpath",
                                    PointerType::getUnqual(i8Ty),
                                    PointerType::getUnqual(i8Ty),
                                    PointerType::getUnqual(i8Ty),
                                    NULL);
    mainModule->getOrInsertFunction("getutent",
                                    PointerType::getUnqual(i8Ty),
                                    NULL);
    mainModule->getOrInsertFunction("__fgetc_unlocked",
                                    Type::getInt32Ty(getGlobalContext()),
                                    PointerType::getUnqual(i8Ty),
                                    NULL);
    mainModule->getOrInsertFunction("__fputc_unlocked",
                                    Type::getInt32Ty(getGlobalContext()),
                                    Type::getInt32Ty(getGlobalContext()),
                                    PointerType::getUnqual(i8Ty),
                                    NULL);
  }

  f = mainModule->getFunction("__ctype_get_mb_cur_max");
  if (f) f->setName("_stdlib_mb_cur_max");

  // Strip of asm prefixes for 64 bit versions because they are not
  // present in uclibc and we want to make sure stuff will get
  // linked. In the off chance that both prefixed and unprefixed
  // versions are present in the module, make sure we don't create a
  // naming conflict.
  for (Module::iterator fi = mainModule->begin(), fe = mainModule->end();
       fi != fe;) {
    Function *f = fi;
    ++fi;
    const std::string &name = f->getName();
    if (name[0]=='\01') {
      unsigned size = name.size();
      if (name[size-2]=='6' && name[size-1]=='4') {
        std::string unprefixed = name.substr(1);

        // See if the unprefixed version exists.
        if (Function *f2 = mainModule->getFunction(unprefixed)) {
          f->replaceAllUsesWith(f2);
          f->eraseFromParent();
        } else {
          f->setName(unprefixed);
        }
      }
    }
  }
  
  mainModule = klee::linkWithLibrary(mainModule, 
                                     KLEE_UCLIBC "/lib/libc.a");
  assert(mainModule && "unable to link with uclibc");

  // more sighs, this is horrible but just a temp hack
  //    f = mainModule->getFunction("__fputc_unlocked");
  //    if (f) f->setName("fputc_unlocked");
  //    f = mainModule->getFunction("__fgetc_unlocked");
  //    if (f) f->setName("fgetc_unlocked");
  
  Function *f2;
  f = mainModule->getFunction("open");
  f2 = mainModule->getFunction("__libc_open");
  if (f2) {
    if (f) {
      f2->replaceAllUsesWith(f);
      f2->eraseFromParent();
    } else {
      f2->setName("open");
      assert(f2->getName() == "open");
    }
  }

  f = mainModule->getFunction("fcntl");
  f2 = mainModule->getFunction("__libc_fcntl");
  if (f2) {
    if (f) {
      f2->replaceAllUsesWith(f);
      f2->eraseFromParent();
    } else {
      f2->setName("fcntl");
      assert(f2->getName() == "fcntl");
    }
  }

  // XXX we need to rearchitect so this can also be used with
  // programs externally linked with uclibc.

  // We now need to swap things so that __uClibc_main is the entry
  // point, in such a way that the arguments are passed to
  // __uClibc_main correctly. We do this by renaming the user main
  // and generating a stub function to call __uClibc_main. There is
  // also an implicit cooperation in that runFunctionAsMain sets up
  // the environment arguments to what uclibc expects (following
  // argv), since it does not explicitly take an envp argument.
  Function *userMainFn = mainModule->getFunction("main");
  assert(userMainFn && "unable to get user main");    
  Function *uclibcMainFn = mainModule->getFunction("__uClibc_main");
  assert(uclibcMainFn && "unable to get uclibc main");    
  userMainFn->setName("__user_main");

  const FunctionType *ft = uclibcMainFn->getFunctionType();
  assert(ft->getNumParams() == 7);

  std::vector<const Type*> fArgs;
  fArgs.push_back(ft->getParamType(1)); // argc
  fArgs.push_back(ft->getParamType(2)); // argv
  Function *stub = Function::Create(FunctionType::get(Type::getInt32Ty(getGlobalContext()), fArgs, false),
      			      GlobalVariable::ExternalLinkage,
      			      "main",
      			      mainModule);
  BasicBlock *bb = BasicBlock::Create(getGlobalContext(), "entry", stub);

  std::vector<llvm::Value*> args;
  args.push_back(llvm::ConstantExpr::getBitCast(userMainFn, 
                                                ft->getParamType(0)));
  args.push_back(stub->arg_begin()); // argc
  args.push_back(++stub->arg_begin()); // argv    
  args.push_back(Constant::getNullValue(ft->getParamType(3))); // app_init
  args.push_back(Constant::getNullValue(ft->getParamType(4))); // app_fini
  args.push_back(Constant::getNullValue(ft->getParamType(5))); // rtld_fini
  args.push_back(Constant::getNullValue(ft->getParamType(6))); // stack_end
  CallInst::Create(uclibcMainFn, args.begin(), args.end(), "", bb);
  
  new UnreachableInst(getGlobalContext(), bb);

  return mainModule;
}

//===----------------------------------------------------------------------===//
// Utility functions 
//===----------------------------------------------------------------------===//
static std::string strip(std::string &in) {
  unsigned len = in.size();
  unsigned lead = 0, trail = len;
  while (lead<len && isspace(in[lead]))
    ++lead;
  while (trail>lead && isspace(in[trail-1]))
    --trail;
  return in.substr(lead, trail-lead);
}

static void readArgumentsFromFile(char *file, std::vector<std::string> &results) {
  std::ifstream f(file);
  assert(f.is_open() && "unable to open input for reading arguments");
  while (!f.eof()) {
    std::string line;
    std::getline(f, line);
    line = strip(line);
    if (!line.empty())
      results.push_back(line);
  }
  f.close();
}

static void parseArguments(int argc, char **argv) {
  std::vector<std::string> arguments;

  for (int i=1; i<argc; i++) {
    if (!strcmp(argv[i],"--read-args") && i+1<argc) {
      readArgumentsFromFile(argv[++i], arguments);
    } else {
      arguments.push_back(argv[i]);
    }
  }

  int numArgs = arguments.size() + 1;
  const char **argArray = new const char*[numArgs+1];
  argArray[0] = argv[0];
  argArray[numArgs] = 0;
  for (int i=1; i<numArgs; i++) {
    argArray[i] = arguments[i-1].c_str();
  }

  llvm::cl::ParseCommandLineOptions(numArgs, (char**) argArray, " klee\n");
  delete[] argArray;
}

// returns the end of the string put in buf
static char *format_tdiff(char *buf, long seconds)
{
  assert(seconds >= 0);

  long minutes = seconds / 60;  seconds %= 60;
  long hours   = minutes / 60;  minutes %= 60;
  long days    = hours   / 24;  hours   %= 24;

  buf = strrchr(buf, '\0');
  if (days > 0) buf += sprintf(buf, "%ld days, ", days);
  buf += sprintf(buf, "%02ld:%02ld:%02ld", hours, minutes, seconds);
  return buf;
}

//===----------------------------------------------------------------------===//
// Interrupt handler 
//===----------------------------------------------------------------------===//
static void interrupt_handle() {
  if (!g_interrupted && g_interpreter) {
    llvm::errs() << "CV: ctrl-c detected, requesting interpreter to halt.\n";
    g_interpreter->setHaltExecution(true);
    llvm::sys::SetInterruptFunction(interrupt_handle);
  } else {
    llvm::errs() << "CV: ctrl-c detected, exiting.\n";
    exit(1);
  }
  g_interrupted = true;
}

//===----------------------------------------------------------------------===//
// main
//===----------------------------------------------------------------------===//
int main(int argc, char **argv, char **envp) {
  using namespace llvm;
#if ENABLE_STPLOG == 1
  STPLOG_init("stplog.c");
#endif
  parseArguments(argc, argv);

	g_client_verifier = new cliver::ClientVerifier();

  atexit(llvm::llvm_shutdown);  // Call llvm_shutdown() on exit.

  // ??
  llvm::InitializeNativeTarget();

  llvm::sys::PrintStackTraceOnErrorSignal();
  llvm::sys::SetInterruptFunction(interrupt_handle);

  std::string error_msg;

  llvm::MemoryBuffer *input_buffer 
    = llvm::MemoryBuffer::getFileOrSTDIN(InputFile,&error_msg);

  if (!input_buffer) 
    cliver::cv_error("%s %s", InputFile.c_str(), error_msg.c_str());

  llvm::Module *main_module = llvm::getLazyBitcodeModule(
      input_buffer, llvm::getGlobalContext(), &error_msg);

  if (main_module) {
    if (main_module->MaterializeAllPermanently(&error_msg)) {
      delete main_module;
      main_module = 0;
    }
  }

  if (!main_module)
    cliver::cv_error("%s %s", InputFile.c_str(), error_msg.c_str());

  llvm::sys::Path LibraryDir(KLEE_DIR "/" RUNTIME_CONFIGURATION "/lib");
  klee::Interpreter::ModuleOptions Opts(LibraryDir.c_str(),
      /*Optimize=*/ false, 
      /*CheckDivZero=*/ false);

  //if (WithPOSIXRuntime) {
	//	cliver::cv_error("posix-runtime is not supported");
  //}

	cliver::cv_message("Checking for POSIX runtime...");
  if (WithPOSIXRuntime) {
    llvm::sys::Path Path(Opts.LibraryDir);
    Path.appendComponent("libkleeRuntimePOSIX.bca");
		cliver::cv_message("NOTE: Using model: %s", Path.c_str());
    main_module = klee::linkWithLibrary(main_module, Path.c_str());
    assert(main_module && "unable to link with simple model");
  }  

  if (WithCliverRuntime) {
    llvm::sys::Path runtime_path(Opts.LibraryDir);
    runtime_path.appendComponent("libCliverRuntime.bca");
		cliver::cv_message("Using runtime %s", runtime_path.c_str());
    main_module = klee::linkWithLibrary(main_module, runtime_path.c_str());
    if (!main_module)
      cliver::cv_error("unable to link with cliver runtime");
  }  

  switch (Libc) {
    case NoLibc: /* silence compiler warning */
      break;

    case KleeLibc: 
      {
        llvm::sys::Path Path(Opts.LibraryDir);
        Path.appendComponent("libklee-libc.bca");
        main_module = klee::linkWithLibrary(main_module, Path.c_str());
        if (!main_module)
          cliver::cv_error("unable to link with klee-libc");
        break;
      }

    case UcLibc:
			main_module = linkWithUclibc(main_module);
			if (!main_module)
				cliver::cv_error("unable to link with uclibc");
      break;
  }

  Function *main_fn = main_module->getFunction("main");
  if (!main_fn)
    cliver::cv_error("'main' function not found in module");

  // FIXME: Change me to std types.
  int pArgc;
  char **pArgv;
  char **pEnvp;
  if (Environ != "") {
    std::vector<std::string> items;
    std::ifstream f(Environ.c_str());
    if (!f.good())
      cliver::cv_error("unable to open --environ file: %s", Environ.c_str());
    while (!f.eof()) {
      std::string line;
      std::getline(f, line);
      line = strip(line);
      if (!line.empty())
        items.push_back(line);
    }
    f.close();
    pEnvp = new char *[items.size()+1];
    unsigned i=0;
    for (; i != items.size(); ++i)
      pEnvp[i] = strdup(items[i].c_str());
    pEnvp[i] = 0;
  } else {
    pEnvp = envp;
  }

  pArgc = InputArgv.size() + 1; 
  pArgv = new char *[pArgc];
  for (unsigned i=0; i<InputArgv.size()+1; i++) {
    std::string &arg = (i==0 ? InputFile : InputArgv[i-1]);
    unsigned size = arg.size() + 1;
    char *pArg = new char[size];

    std::copy(arg.begin(), arg.end(), pArg);
    pArg[size - 1] = 0;

    pArgv[i] = pArg;
  }

  klee::Interpreter::InterpreterOptions IOpts;
  IOpts.MakeConcreteSymbolic = false;
	g_interpreter = new cliver::CVExecutor(IOpts, g_client_verifier);

  // Print args to info file
  std::ostream &infoFile = g_client_verifier->getInfoStream();
  for (int i=0; i<argc; i++) {
    infoFile << argv[i] << (i+1<argc ? " ":"\n");
  }
  infoFile << "PID: " << getpid() << "\n";

  const Module *final_module = g_interpreter->setModule(main_module, Opts);
  //externalsAndGlobalsCheck(final_module);

  // Start time
  char buf[256];
  time_t t[2];
  t[0] = time(NULL);
  strftime(buf, sizeof(buf), 
      "Started: %Y-%m-%d %H:%M:%S\n", localtime(&t[0]));
  infoFile << buf;
  infoFile.flush();

  g_interpreter->runFunctionAsMain(main_fn, pArgc, pArgv, pEnvp);

  // End time
  t[1] = time(NULL);
  strftime(buf, sizeof(buf), 
      "Finished: %Y-%m-%d %H:%M:%S\n", localtime(&t[1]));
  infoFile << buf;

  strcpy(buf, "Elapsed: ");
  strcpy(format_tdiff(buf, t[1] - t[0]), "\n");
  infoFile << buf;

  delete g_client_verifier;
  return 0;
}

