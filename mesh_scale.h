/*
 * mesh_scale.h
 *
 * Scale an OBJ mesh uniformly and write the result to a new OBJ file.
 * Corresponds to scale_model() in master_converter_script_mm2espr.py.
 *
 * Source: translated from Python original for obj2msh
 */

#pragma once
#include <string>

/**
 * Load input_path (.obj), apply a uniform scale multiplier on all axes,
 * and write the scaled mesh to output_path (.obj).
 *
 * Prints a before/after extents report to stdout.
 * Also renames the .mtl sidecar to the correct name if one is produced.
 *
 * @param input_path   Path to the source .obj file
 * @param output_path  Path to write the scaled .obj file
 * @param multiplier   Uniform scale factor (e.g. 1000.0 for m→mm)
 *
 * @throws std::runtime_error on I/O or parse errors
 */
void scale_model(const std::string& input_path,
                 const std::string& output_path,
                 double             multiplier);
