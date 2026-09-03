/*
 * epsr_calc.h
 *
 * Calculate the optimal fTetWild epsilon-relative (epsr) value from a
 * user-supplied smallest-feature size (in mesh units / mm).
 *
 * Corresponds to calculate_epsr_from_feature() in
 * master_converter_script_mm2espr.py.
 *
 * Source: translated from Python original for obj2msh
 */

#pragma once
#include <string>

/**
 * Load the mesh at mesh_path, compute its bounding-box diagonal, and return
 * the epsr value that preserves features of size smallest_feature_mm.
 *
 * @param mesh_path           Path to the STL/OBJ mesh (after repair step)
 * @param smallest_feature_mm Smallest feature to preserve, in mesh units
 * @param out_bbox_diag       If non-null, receives the bounding-box diagonal
 * @param out_pct_display     If non-null, receives feature / diag * 100
 * @return recommended epsr value
 *
 * @throws std::runtime_error on I/O errors
 */
double calculate_epsr_from_feature(const std::string& mesh_path,
                                   double             smallest_feature_mm,
                                   double*            out_bbox_diag    = nullptr,
                                   double*            out_pct_display  = nullptr);
