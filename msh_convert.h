/*
 * msh_convert.h
 *
 * Convert a Gmsh MSH file (any version) to ASCII MSH v2.2 with physical tags.
 *
 * Corresponds to convert_msh_to_v22() in master_converter_script_mm2espr.py.
 *
 * Source: translated from Python/meshio logic for obj2msh
 */

#pragma once
#include <string>

/**
 * Read input_file (MSH v4.1 from fTetWild), extract nodes and tetrahedra,
 * and write output_file as ASCII MSH v2.2 with the given physical_tag on all
 * tetrahedral elements.
 *
 * The output is SOFA-compatible (ASCII, v2.2, double precision).
 *
 * @param input_file   Path to fTetWild MSH v4.1 output
 * @param output_file  Path for the final MSH v2.2 ASCII file
 * @param physical_tag Material ID to assign to all elements (default 1)
 *
 * @throws std::runtime_error on parse or I/O errors
 */
void convert_msh_to_v22(const std::string& input_file,
                        const std::string& output_file,
                        int                physical_tag = 1);
