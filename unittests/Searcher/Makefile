##===- unittests/Searcher/Makefile -------------------------*- Makefile -*-===##

LEVEL := ../..
include $(LEVEL)/Makefile.config

TESTNAME := Searcher
USEDLIBS := Cliver.a CliverSerialization.a kleeCore.a kleeBasic.a kleeModule.a kleaverSolver.a kleaverExpr.a kleeSupport.a 
LINK_COMPONENTS := jit bitreader bitwriter ipo linker engine support core

include $(LLVM_SRC_ROOT)/unittests/Makefile.unittest

CPP.Flags += -DGTEST_PRINT_TIME=1

LIBS += -lstp
