#===------------------------------------------------------------------------===#
#
#                     The KLEE Symbolic Virtual Machine
#
# This file is distributed under the University of Illinois Open Source
# License. See LICENSE.TXT for details.
#
#===------------------------------------------------------------------------===#
klee_add_component(cliverCore
  ClientVerifier.cpp
  CVCommon.h
)

# TODO: Work out what the correct LLVM components are for
# kleeCore.
set(LLVM_COMPONENTS
  core
  support
)

if ("${LLVM_PACKAGE_VERSION}" VERSION_EQUAL "3.6" OR
    "${LLVM_PACKAGE_VERSION}" VERSION_GREATER "3.6")
  list(APPEND LLVM_COMPONENTS mcjit executionengine native)
else()
  list(APPEND LLVM_COMPONENTS jit engine)
endif()


klee_get_llvm_libs(LLVM_LIBS ${LLVM_COMPONENTS})
target_link_libraries(cliverCore
    PUBLIC
    ${LLVM_LIBS}
)

target_include_directories(cliverCore PUBLIC
    ${SVF_ROOT_DIR}/include
    ${DG_ROOT_DIR}/src
    ${DG_ROOT_DIR}/tools
    /playpen/nesfield/2019_project/tetrinet_skip/chop_net3/gsec_install/local/include/
#    ${FOLLY_ROOT_DIR}/
)
target_link_libraries(cliverCore PRIVATE
  kleeAnalysis
  kleeBasic
  kleeModule
  kleaverSolver
  kleaverExpr
  kleeSupport
)
