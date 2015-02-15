#===-- tools/Makefile --------------------------------------*- Makefile -*--===#
#
#                     The KLEE Symbolic Virtual Machine
#
# This file is distributed under the University of Illinois Open Source
# License. See LICENSE.TXT for details.
#
#===------------------------------------------------------------------------===#

#
# Relative path to the top of the source tree.
#
LEVEL=..

#
# List all of the subdirectories that we will compile.
#
PARALLEL_DIRS=klee kleaver ktest-tool gen-random-bout klee-stats cliverstats hmmtool hmmtrain

include $(LEVEL)/Makefile.config

ifeq ($(ENABLE_POSIX_RUNTIME),1)
PARALLEL_DIRS += klee-replay
endif

# we only support compiling klee, cliverstats, and hmmtool with ThreadSanitizer
ifeq ($(ENABLE_THREAD_SANITIZER),1)
  PARALLEL_DIRS=klee kleaver cliverstats hmmtool
endif


include $(LEVEL)/Makefile.common
