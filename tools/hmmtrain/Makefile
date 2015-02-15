#===-- tools/klee-stats/Makefile -----------------------*- Makefile -*--===#
#
#                     The KLEE Symbolic Virtual Machine
#
# This file is distributed under the University of Illinois Open Source
# License. See LICENSE.TXT for details.
#
#===------------------------------------------------------------------------===#

LEVEL = ../..

TOOLSCRIPTNAME := hmmtrain

HELPER_SCRIPTS += distmtx.py
HELPER_SCRIPTS += extract_training.py
HELPER_SCRIPTS += levenshtein.py
HELPER_SCRIPTS += message_features.py
HELPER_SCRIPTS += hier_cluster.py
HELPER_SCRIPTS += hmm_estimate.py
HELPER_SCRIPTS_ABS = $(addprefix $(PROJ_SRC_DIR)/,$(HELPER_SCRIPTS))

# Hack to prevent install trying to strip
# symbols from a python script
KEEP_SYMBOLS := 1

include $(LEVEL)/Makefile.common

# FIXME: Move this stuff (to "build" a script) into Makefile.rules.

ToolBuildPath := $(ToolDir)/$(TOOLSCRIPTNAME)

all-local:: $(ToolBuildPath)

$(ToolBuildPath): $(ToolDir)/.dir

$(ToolBuildPath): $(PROJ_SRC_DIR)/$(TOOLSCRIPTNAME)
	$(Echo) Copying $(BuildMode) script $(TOOLSCRIPTNAME)
	$(Verb) $(CP) -f $(PROJ_SRC_DIR)/$(TOOLSCRIPTNAME) "$@"
	$(Verb) chmod 0755 "$@"

ifdef NO_INSTALL
install-local::
	$(Echo) Install circumvented with NO_INSTALL
uninstall-local::
	$(Echo) Uninstall circumvented with NO_INSTALL
else
DestTool = $(DESTDIR)$(PROJ_bindir)/$(TOOLSCRIPTNAME)
DestHelpers = $(DESTDIR)$(PROJ_bindir)/

install-local:: $(DestTool)

$(DestTool): $(ToolBuildPath) $(DESTDIR)$(PROJ_bindir)
	$(Echo) Installing $(BuildMode) $(DestTool)
	$(Verb) $(ScriptInstall) $(ToolBuildPath) $(DestTool)
	$(Verb) $(ScriptInstall) $(HELPER_SCRIPTS_ABS) $(DestHelpers)

uninstall-local::
	$(Echo) Uninstalling $(BuildMode) $(DestTool)
	-$(Verb) $(RM) -f $(DestTool)
endif
