#include "Util.h"
#include "llvm/Support/CommandLine.h"
#include "klee/ExecutionState.h"


std::string get_instruction_data(llvm::Instruction* target_ins){
  assert(target_ins  != NULL);
  const char *function_name = target_ins->getParent()->getParent()->getName().data();
  assert(function_name  != NULL);
  llvm::MDNode *metadata = target_ins->getMetadata("dbg");
  if (!metadata) {
    std::cout << "Instruction missing metadata\n";
    return "";
  }

  llvm::DILocation loc(metadata); // DILocation is in DebugInfo.h
  const char  *file      = loc.getFilename().data();
  const char  *dir       = loc.getDirectory().data();
  int          line_num  = loc.getLineNumber();
  if(file == NULL || dir == NULL || line_num < 1){
    std::cout << "Instruction missing metadata\n";
    return "";
  }

  std::string ret = dir;
  ret += "/";
  ret += file;
  ret += ":" + std::to_string(line_num);
  return ret;

}

