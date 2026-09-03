/*
 * tetwild_runner.h
 *
 * Invoke the FloatTetwild volumetric mesher on a surface mesh and produce
 * a tetrahedral MSH v4.1 file.
 *
 * FloatTetwild is fetched and built from source as a CMake sub-project:
 *   https://github.com/wildmeshing/fTetWild
 *
 * Source: written for obj2msh
 */

#pragma once
#include <string>

/**
 * Run the FloatTetwild mesher as a subprocess.
 *
 * The FloatTetwild binary (FloatTetwild_bin) is expected either:
 *   - at the path stored in the OBJ2MSH_FTETWILD_BIN environment variable, or
 *   - built beside this executable at build time (see CMakeLists.txt).
 *
 * @param input_stl    Path to the repaired STL surface mesh
 * @param output_msh   Path where fTetWild should write its .msh output
 * @param epsr         Relative envelope tolerance (dimensionless, e.g. 0.002)
 *
 * @throws std::runtime_error if the binary cannot be found or meshing fails
 */
void run_tetwild(const std::string& input_stl,
                 const std::string& output_msh,
                 double             epsr);
