/*
 * mesh_repair.h
 *
 * Pre-process a surface mesh for solid volumetric output:
 *   1. Close holes
 *   2. Laplacian smoothing
 *   3. Remove unreferenced vertices
 *   4. Export as STL (for fTetWild)
 *
 * Corresponds to repair_mesh() in master_converter_script_mm2espr.py.
 *
 * Source: translated from Python original for obj2msh
 */

#pragma once
#include <string>

/**
 * Repair the surface mesh at input_file and write the result to output_file.
 *
 * @param input_file    Path to an OBJ (or any supported) surface mesh
 * @param output_file   Path to write the repaired STL file
 * @param max_hole_size Maximum number of boundary edges in a hole to fill
 *                      (default 1000 – mirrors the Python default)
 * @param smooth_steps  Number of Laplacian smoothing iterations
 *                      (default 3 – mirrors the Python default)
 *
 * @throws std::runtime_error on I/O or geometry errors
 */
void repair_mesh(const std::string& input_file,
                 const std::string& output_file,
                 int                max_hole_size = 1000,
                 int                smooth_steps  = 3);
