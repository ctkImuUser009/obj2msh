/*
 * msh_verify.cpp
 *
 * Sanity check a .msh file for validity and SOFA compatibility.
 * Translated from check_msh_integrity() in master_converter_script_mm2espr.py.
 *
 * Gmsh MSH v2.2 format reference:
 *   https://gmsh.info/doc/texinfo/gmsh.html#MSH-file-format-version-2
 *
 * Source: translated from Python original for obj2msh
 */

#include "msh_verify.h"

#include <fstream>
#include <sstream>
#include <iostream>
#include <string>
#include <stdexcept>

MshCheckResult check_msh_integrity(const std::string& file_path)
{
    MshCheckResult result;

    std::cout << "\n" << std::string(50, '=') << "\n";
    std::cout << "      DETAILED MESH INTEGRITY & VERSION REPORT\n";
    std::cout << std::string(50, '=') << "\n";

    std::ifstream fin(file_path);
    if (!fin.is_open()) {
        std::cout << "!!ERROR: File not found at " << file_path << "\n";
        std::cout << std::string(50, '=') << "\n\n";
        return result;
    }

    // ── Read the $MeshFormat header ──────────────────────────────────────────
    {
        std::string line;
        while (std::getline(fin, line)) {
            if (line == "$MeshFormat") {
                std::string fmt_line;
                std::getline(fin, fmt_line);
                std::istringstream ss(fmt_line);
                std::string ver;
                int file_type;
                if (ss >> ver >> file_type) {
                    result.version  = ver;
                    result.is_ascii = (file_type == 0);
                }
                break;
            }
        }
    }
    fin.clear();
    fin.seekg(0);

    // ── Count nodes and tetrahedra ───────────────────────────────────────────
    {
        std::string line;
        bool in_nodes = false, in_elems = false;
        while (std::getline(fin, line)) {
            if (line == "$Nodes")       { in_nodes = true;  continue; }
            if (line == "$EndNodes")    { in_nodes = false; continue; }
            if (line == "$Elements")    { in_elems = true;  continue; }
            if (line == "$EndElements") { in_elems = false; continue; }

            if (in_nodes && result.num_nodes == 0) {
                // First line after $Nodes is the count
                try { result.num_nodes = std::stoi(line); } catch (...) {}
                in_nodes = false; // Don't count again
            }
            if (in_elems && result.num_tets == 0) {
                // Count tetra lines (type == 4) within $Elements block
                try {
                    int total = std::stoi(line);
                    // Now read element lines
                    for (int i = 0; i < total; ++i) {
                        std::string el;
                        std::getline(fin, el);
                        std::istringstream ss(el);
                        int id, type;
                        ss >> id >> type;
                        if (type == 4) result.num_tets++;
                    }
                    in_elems = false;
                } catch (...) {}
            }
        }
    }

    // ── Print report ─────────────────────────────────────────────────────────
    std::cout << "File Path:      " << file_path        << "\n";
    std::cout << "GMSH Version:   " << result.version   << "\n";
    std::cout << "Format Type:    " << (result.is_ascii ? "ASCII" : "Binary") << "\n";

    if (result.version != "2.2" && result.version != "Unknown") {
        std::cout << "\n" << std::string(50, '!') << "\n";
        std::cout << "!! ALERT: LIMITED COMPATIBILITY DETECTED!\n";
        std::cout << "Current version is " << result.version
                  << ". Many legacy solvers and loaders\n"
                     "require v2.2 and may fail to parse this file.\n";
        std::cout << std::string(50, '!') << "\n\n";
        result.compatible = false;
    } else if (result.version == "Unknown") {
        std::cout << "Format Check:   [WARNING] Version metadata not found in file header\n";
        result.compatible = false;
    } else {
        std::cout << "Format Check:   [OK] v2.2 Confirmed (High Compatibility)\n";
        result.compatible = true;
    }

    std::cout << "Number of Nodes: " << result.num_nodes << "\n";
    std::cout << "Tetrahedra:      " << result.num_tets  << "\n";
    std::cout << std::string(50, '-') << "\n";

    if (result.num_nodes == 0) {
        std::cout << "RESULT: !!FAILED (No geometry)\n";
        result.valid = false;
    } else if (result.num_tets == 0) {
        std::cout << "RESULT: !!FAILED (No volumetric tetrahedra found)\n";
        result.valid = false;
    } else if (result.version == "Unknown") {
        std::cout << "RESULT: [WARNING] (Geometry valid, but version metadata missing)\n";
        result.valid = true;
    } else if (result.version != "2.2") {
        std::cout << "RESULT: [WARNING] (Geometry valid, but limited compatibility)\n";
        result.valid = true;
    } else {
        std::cout << "RESULT: [OK] SUCCESS (File is healthy and widely compatible)\n";
        result.valid = true;
    }

    std::cout << std::string(50, '=') << "\n\n";
    return result;
}
