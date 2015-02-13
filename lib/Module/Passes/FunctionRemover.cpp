#include "../Passes.h"
#include "FunctionRemover.h"

#include "llvm/Support/CallSite.h"
#include "llvm/Support/raw_ostream.h"

char klee::FunctionRemover::ID = 0;
static llvm::RegisterPass<klee::FunctionRemover> RP_functionremover(
    "functionremover", "Function Remover Pass (klee)", false, false);

    //CallInst *CI = dyn_cast<CallInst>(&*i);
    // increment now since LowerIntrinsic deletion makes iterator invalid.
    //++i;  

    //if (CI) {
    //  CallSite cs(CI);
    //  if (Function *f = dyn_cast<Function>(cs.getCalledValue())) {
    //    if (f->getName().str()[0] == 'X' && f->getName().str().substr(0,6)!= "Xpilot") {
    //      errs() << "XFunction: " << f->getName().str() << ": " << *(f->getReturnType()) << "\n";
    //      if (f->getReturnType() == Type::getInt32Ty(getGlobalContext())) {
    //        IRBuilder<> builder(CI->getParent(), CI);
    //        errs() << "XFunction: " << f->getName().str() << "\n";
    //        //FunctionType *FTy = FunctionType::get(Type::getInt32Ty(getGlobalContext()), false);
    //        FunctionType *FTy = FunctionType::get(f->getReturnType(), false);
    //        Function *F = cast<Function>(M.getOrInsertFunction("get_zero", FTy));
    //        //F->addAttribute(AttributeSet::FunctionIndex, Attribute::ReadOnly);
    //        
    //        //StoreInst *S = builder.CreateStore(ConstantInt::get(Type::getInt32Ty(getGlobalContext()), 0),
    //        //                                    CI, false);

    //        //new LoadInst(ConstantInt::get(Type::getInt32Ty(getGlobalContext()), 0), "XFunc", CI);
    //        //new StoreInst(load, castedDst, false, ii);
    //        //new StoreInst(load, castedDst, false, ii);
    //        //new StoreInst(ConstantInt::get(Type::getInt32Ty(getGlobalContext()), 0),
    //        //                                    CI, false);
    //        //Value *load = new LoadInst(castedSrc, "vacopy.read", ii);
    //
    //        CI->replaceAllUsesWith(CallInst::Create(F, Twine(), CI));

    //        CI->eraseFromParent();

    //        dirty = true;
    //      }
    //
    //    }
    //  }
    //}
    

