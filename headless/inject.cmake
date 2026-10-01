# SPDX-License-Identifier: GPL-3.0-or-later
# Add our frontend after the pinned project's real core targets exist.
set(EDEN_PORT_DIR "${CMAKE_CURRENT_LIST_DIR}")
function(add_eden_headless)
    include("${EDEN_PORT_DIR}/CMakeLists.txt")
endfunction()
cmake_language(DEFER CALL add_eden_headless)
# One header is replaced for every target: common/sparse_large_vector.h, whose inline functions
# decide how Eden's large tables take their memory (headless/CMakeLists.txt writes the derived
# copy). This runs before any target exists, so the folder leads every include path: a source
# file that saw the original would mix two ways of committing the same table.
include_directories(BEFORE "${CMAKE_BINARY_DIR}/headless/sparse")
# The program names its sources in log lines and assertions. In a console build they are written
# relative to the trees they come from (src/..., build/..., headless/...), not with the folders of
# the machine that built it: a package must not carry those. Before any target exists, so it holds
# for every one.
if(PS5_NATIVE)
    get_filename_component(EDEN_PORT_ROOT "${EDEN_PORT_DIR}/.." ABSOLUTE)
    add_compile_options("-ffile-prefix-map=${CMAKE_SOURCE_DIR}/="
                        "-ffile-prefix-map=${CMAKE_BINARY_DIR}/=build/"
                        "-ffile-prefix-map=${EDEN_PORT_ROOT}/=")
endif()
