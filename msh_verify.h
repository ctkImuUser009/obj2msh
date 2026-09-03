/*
 * msh_verify.h
 *
 * Integrity and compatibility check for .msh files.
 * Corresponds to check_msh_integrity() in master_converter_script_mm2espr.py.
 *
 * Source: translated from Python original for obj2msh
 */

#pragma once
#include <string>

struct MshCheckResult {
    bool        valid      = false;
    std::string version    = "Unknown";
    bool        is_ascii   = false;
    int         num_nodes  = 0;
    int         num_tets   = 0;
    bool        compatible = false;  // true only if v2.2
};

/**
 * Check the integrity and SOFA compatibility of a .msh file.
 *
 * @param file_path  Path to the .msh file to inspect
 * @return MshCheckResult struct with check outcomes
 */
MshCheckResult check_msh_integrity(const std::string& file_path);
