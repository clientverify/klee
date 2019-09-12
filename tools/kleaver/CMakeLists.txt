#===------------------------------------------------------------------------===#
#
#                     The KLEE Symbolic Virtual Machine
#
# This file is distributed under the University of Illinois Open Source
# License. See LICENSE.TXT for details.
#
#===------------------------------------------------------------------------===#
add_executable(kleaver
  main.cpp
)

set(KLEE_LIBS
  kleaverSolver
)

find_package(Boost COMPONENTS system filesystem REQUIRED)
target_link_libraries(kleaver
    ${KLEE_LIBS}
    ${Boost_LIBRARY_DIRS}
    ${Boost_SYSTEM_LIBRARY}
    ${Boost_FILESYSTEM_LIBRARY}
)

install(TARGETS kleaver RUNTIME DESTINATION bin)
