#include "llvm/IR/Function.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/DebugInfo.h"
#include "llvm/Support/raw_ostream.h"

const char* get_instruction_directory(llvm::Instruction* target_ins);
int get_instruction_line_num(llvm::Instruction* target_ins);
const char* get_function_directory(llvm::Function* target);
llvm::raw_fd_ostream* get_fd_ostream(std::string path);
