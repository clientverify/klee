#include "Util.h"
#include "llvm/Support/CommandLine.h"
#include "klee/ExecutionState.h"

#define DEBUG_FUNCTION_DIR 0
const char* get_instruction_directory(llvm::Instruction* target_ins){
  assert(target_ins  != NULL);
  const char *function_name = target_ins->getParent()->getParent()->getName().data();
  assert(function_name  != NULL);
  llvm::MDNode *metadata = target_ins->getMetadata("dbg");
  if (!metadata) {
    if(DEBUG_FUNCTION_DIR) printf("function_call/return not adding info for %s no metadata\n", function_name);
    return NULL;
  }

  llvm::DILocation loc(metadata); // DILocation is in DebugInfo.h
  const char  *dir  = loc.getDirectory().data();
  return dir;
}

const char* get_function_directory(llvm::Function* target){
  assert(target != NULL);
  const char *target_name = target->getName().data();
  assert(target_name != NULL);
  if(target->size() <= 0){
    if(DEBUG_FUNCTION_DIR) printf("function_call %s has no basic blocks\n", target_name);
    return NULL;
  }
  llvm::Instruction *target_ins = target->getEntryBlock().begin();
  assert(target_ins  != NULL);
  return get_instruction_directory(target_ins);
}

llvm::raw_fd_ostream* get_fd_ostream(std::string path) {
  llvm::raw_fd_ostream *f;
  std::string Error;
#if LLVM_VERSION_CODE >= LLVM_VERSION(3,5)
  f = new llvm::raw_fd_ostream(path.c_str(), Error, llvm::sys::fs::F_None);
#elif LLVM_VERSION_CODE >= LLVM_VERSION(3,4)
  f = new llvm::raw_fd_ostream(path.c_str(), Error, llvm::sys::fs::F_Binary);
#else
  f = new llvm::raw_fd_ostream(path.c_str(), Error, llvm::raw_fd_ostream::F_Binary);
#endif
  assert(f);
  if (!Error.empty()) {
    printf("error opening file \"%s\".  KLEE may have run out of file "
        "descriptors: try to increase the maximum number of open file "
        "descriptors by using ulimit (%s).",
        path.c_str(), Error.c_str());
    delete f;
    f = NULL;
  }
  return f;
}
