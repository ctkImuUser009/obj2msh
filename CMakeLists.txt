# ============================================================================
# CMakeLists.txt  –  obj2msh
#
# Builds the obj2msh pipeline tool and, as a sub-project, fetches and
# compiles FloatTetwild from source:
#   https://github.com/wildmeshing/fTetWild
# (Copyright (C) 2019 Yixin Hu, MPL 2.0)
#
# Usage:
#   mkdir build && cd build
#   cmake .. -DCMAKE_BUILD_TYPE=Release
#   make -j$(nproc)
#
# The build produces:
#   build/obj2msh                    – Linux ELF executable
#   build/ftetwild_install/bin/FloatTetwild_bin  – tetwild binary (auto-found)
# ============================================================================
cmake_minimum_required(VERSION 3.16)
project(obj2msh CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ─────────────────────────────────────────────────────────────────────────────
# Find system dependencies for obj2msh itself
# ─────────────────────────────────────────────────────────────────────────────
find_package(Eigen3 3.3 REQUIRED NO_MODULE)

# ncurses (for the interactive file picker on POSIX systems)
if(NOT WIN32)
    find_package(Curses QUIET)
endif()

# ─────────────────────────────────────────────────────────────────────────────
# Fetch and build FloatTetwild
#
# FloatTetwild source: https://github.com/wildmeshing/fTetWild
# We build it as an ExternalProject so it gets its own dependency chain
# (geogram, GMP, MPFR, libigl, spdlog, etc.) downloaded automatically.
# ─────────────────────────────────────────────────────────────────────────────
include(ExternalProject)

set(FTETWILD_INSTALL_DIR "${CMAKE_BINARY_DIR}/ftetwild_install")
set(FTETWILD_BIN         "${FTETWILD_INSTALL_DIR}/bin/FloatTetwild_bin")

ExternalProject_Add(
    FloatTetwild
    GIT_REPOSITORY  https://github.com/wildmeshing/fTetWild.git
    GIT_TAG         main
    GIT_SHALLOW     TRUE
    PREFIX          "${CMAKE_BINARY_DIR}/ftetwild_ep"
    CMAKE_ARGS
        -DCMAKE_INSTALL_PREFIX=${FTETWILD_INSTALL_DIR}
        -DCMAKE_BUILD_TYPE=Release
        -DFLOAT_TETWILD_BUILD_TESTS=OFF
    BUILD_COMMAND   ${CMAKE_COMMAND} --build . --target FloatTetwild_bin --config Release
    INSTALL_COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "<BINARY_DIR>/FloatTetwild_bin"
                    "${FTETWILD_BIN}"
    LOG_DOWNLOAD  ON
    LOG_BUILD     ON
)

# Create the install dir so CMake's copy command can run even on first config
file(MAKE_DIRECTORY "${FTETWILD_INSTALL_DIR}/bin")

# ─────────────────────────────────────────────────────────────────────────────
# obj2msh executable
# ─────────────────────────────────────────────────────────────────────────────
set(OBJ2MSH_SOURCES
    src/main.cpp
    src/file_picker.cpp
    src/mesh_scale.cpp
    src/mesh_repair.cpp
    src/epsr_calc.cpp
    src/tetwild_runner.cpp
    src/msh_convert.cpp
    src/msh_verify.cpp
    src/glibc_compat.c          # glibc 2.35 (Jammy) compatibility shim
)

add_executable(obj2msh ${OBJ2MSH_SOURCES})

target_include_directories(obj2msh PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(obj2msh PRIVATE
    Eigen3::Eigen
)

# ncurses (Linux/macOS file picker)
if(NOT WIN32 AND CURSES_FOUND)
    target_link_libraries(obj2msh PRIVATE ${CURSES_LIBRARIES})
    target_include_directories(obj2msh PRIVATE ${CURSES_INCLUDE_DIRS})
    target_compile_definitions(obj2msh PRIVATE HAVE_NCURSES)
elseif(NOT WIN32)
    # Try to link libncurses directly as a fallback
    target_link_libraries(obj2msh PRIVATE ncurses)
endif()

# Tell obj2msh where to find the FloatTetwild binary at runtime
# (baked into the binary as a compile-time fallback path)
target_compile_definitions(obj2msh PRIVATE
    OBJ2MSH_FTETWILD_BINARY_PATH="${FTETWILD_BIN}"
)

# Ensure FloatTetwild is built before obj2msh is linked
add_dependencies(obj2msh FloatTetwild)

# ─────────────────────────────────────────────────────────────────────────────
# Compiler flags
# ─────────────────────────────────────────────────────────────────────────────
if(MSVC)
    target_compile_options(obj2msh PRIVATE /W3 /O2)
else()
    target_compile_options(obj2msh PRIVATE -Wall -Wextra -O2)
endif()

# Link stdc++fs on older GCC (pre-9)
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS "9.0")
        target_link_libraries(obj2msh PRIVATE stdc++fs)
    endif()
endif()

# ─────────────────────────────────────────────────────────────────────────────
# Install
# ─────────────────────────────────────────────────────────────────────────────
install(TARGETS obj2msh DESTINATION bin)
install(PROGRAMS ${FTETWILD_BIN} DESTINATION bin OPTIONAL)

# ─────────────────────────────────────────────────────────────────────────────
# Summary
# ─────────────────────────────────────────────────────────────────────────────
message(STATUS "")
message(STATUS "=== obj2msh build configuration ===")
message(STATUS "  C++ standard   : C++17")
message(STATUS "  Eigen3         : ${EIGEN3_INCLUDE_DIR}")
message(STATUS "  ncurses        : ${CURSES_FOUND}")
message(STATUS "  FloatTetwild   : fetched from https://github.com/wildmeshing/fTetWild")
message(STATUS "  fTetWild bin   : ${FTETWILD_BIN}")
message(STATUS "")
