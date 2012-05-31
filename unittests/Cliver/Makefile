##===- unittests/Expr/Makefile -----------------------------*- Makefile -*-===##

LEVEL := ../..
TESTNAME := Cliver
STP_LIBS := stp_c_interface.a stp_AST.a stp_bitvec.a \
            stp_constantbv.a stp_sat.a stp_simplifier.a
USEDLIBS = Cliver.a kleeCore.a kleeModule.a kleaverSolver.a \
					 kleaverExpr.a kleeSupport.a kleeBasic.a $(STP_LIBS)

LINK_COMPONENTS = jit bitreader bitwriter ipo linker engine support

include $(LEVEL)/Makefile.config
include $(LLVM_SRC_ROOT)/unittests/Makefile.unittest

CPP.Flags += -DGTEST_HAS_RTTI=1                                                                                                                                                                                                                                                   
CPP.Flags += -DGTEST_PRINT_TIME=1

