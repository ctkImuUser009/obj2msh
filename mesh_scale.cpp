/*
 * mesh_scale.cpp
 *
 * Uniform scaling of an OBJ mesh, translated from scale_model() in
 * master_converter_script_mm2espr.py.
 *
 * Reads vertex (v) lines, applies the scale multiplier, and writes a new
 * OBJ.  All non-vertex lines (normals, texture coords, faces, material
 * references, comments) are preserved verbatim.
 *
 * Source: translated from Python original for obj2msh
 */

#include "mesh_scale.h"

#include <fstream>
#include <sstream>
#include <iostream>
#include <string>
#include <vector>
#include <array>
#include <limits>
#include <stdexcept>
#include <filesystem>
#include <cmath>

namespace fs = std::filesystem;

// ─────────────────────────────────────────────────────────────────────────────
// Internal: parse a vertex line "v x y z [w]"
// Returns false if the line is not a vertex line.
// ─────────────────────────────────────────────────────────────────────────────
static bool parse_vertex(const std::string& line, double& x, double& y, double& z)
{
    if (line.size() < 2) return false;
    if (line[0] != 'v' || line[1] != ' ') return false;

    std::istringstream ss(line.substr(2));
    if (!(ss >> x >> y >> z)) return false;
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
void scale_model(const std::string& input_path,
                 const std::string& output_path,
                 double             multiplier)
{
    // ── Read the entire OBJ into memory ──────────────────────────────────────
    std::ifstream fin(input_path);
    if (!fin.is_open())
        throw std::runtime_error("scale_model: cannot open input: " + input_path);

    struct Line {
        std::string raw;
        bool        is_vertex = false;
        double      x = 0, y = 0, z = 0;
    };

    std::vector<Line> lines;
    std::string       raw_line;

    // Bounding box before scaling
    double xmin =  std::numeric_limits<double>::max();
    double ymin =  std::numeric_limits<double>::max();
    double zmin =  std::numeric_limits<double>::max();
    double xmax = -std::numeric_limits<double>::max();
    double ymax = -std::numeric_limits<double>::max();
    double zmax = -std::numeric_limits<double>::max();

    while (std::getline(fin, raw_line)) {
        Line ln;
        ln.raw = raw_line;
        if (parse_vertex(raw_line, ln.x, ln.y, ln.z)) {
            ln.is_vertex = true;
            if (ln.x < xmin) xmin = ln.x;
            if (ln.y < ymin) ymin = ln.y;
            if (ln.z < zmin) zmin = ln.z;
            if (ln.x > xmax) xmax = ln.x;
            if (ln.y > ymax) ymax = ln.y;
            if (ln.z > zmax) zmax = ln.z;
        }
        lines.push_back(std::move(ln));
    }
    fin.close();

    // Print extents before
    double dx_b = xmax - xmin;
    double dy_b = ymax - ymin;
    double dz_b = zmax - zmin;

    std::cout << std::string(60, '=') << "\n";
    std::cout << "MODEL SCALE REPORT: " << input_path << "\n";
    std::cout << std::string(60, '=') << "\n";
    std::cout << "EXTENTS BEFORE SCALING:\n";
    std::cout << "  X: " << dx_b << "\n";
    std::cout << "  Y: " << dy_b << "\n";
    std::cout << "  Z: " << dz_b << "\n";
    std::cout << std::string(60, '-') << "\n";

    // ── Scale vertices and compute new bounding box ───────────────────────────
    double ax_xmin =  std::numeric_limits<double>::max();
    double ax_ymin =  std::numeric_limits<double>::max();
    double ax_zmin =  std::numeric_limits<double>::max();
    double ax_xmax = -std::numeric_limits<double>::max();
    double ax_ymax = -std::numeric_limits<double>::max();
    double ax_zmax = -std::numeric_limits<double>::max();

    for (auto& ln : lines) {
        if (!ln.is_vertex) continue;
        ln.x *= multiplier;
        ln.y *= multiplier;
        ln.z *= multiplier;
        if (ln.x < ax_xmin) ax_xmin = ln.x;
        if (ln.y < ax_ymin) ax_ymin = ln.y;
        if (ln.z < ax_zmin) ax_zmin = ln.z;
        if (ln.x > ax_xmax) ax_xmax = ln.x;
        if (ln.y > ax_ymax) ax_ymax = ln.y;
        if (ln.z > ax_zmax) ax_zmax = ln.z;
    }

    double dx_a = ax_xmax - ax_xmin;
    double dy_a = ax_ymax - ax_ymin;
    double dz_a = ax_zmax - ax_zmin;

    std::cout << "APPLIED MULTIPLIER: " << multiplier << "x\n";
    std::cout << "EXTENTS AFTER SCALING:\n";
    std::cout << "  X: " << dx_a << "\n";
    std::cout << "  Y: " << dy_a << "\n";
    std::cout << "  Z: " << dz_a << "\n";
    std::cout << std::string(60, '-') << "\n";

    // ── Write scaled OBJ ──────────────────────────────────────────────────────
    std::ofstream fout(output_path);
    if (!fout.is_open())
        throw std::runtime_error("scale_model: cannot write output: " + output_path);

    fout.precision(10);
    for (const auto& ln : lines) {
        if (ln.is_vertex) {
            fout << "v " << ln.x << " " << ln.y << " " << ln.z << "\n";
        } else {
            fout << ln.raw << "\n";
        }
    }
    fout.close();

    // ── Rename wrongly-suffixed .mtl if pymeshlab-style sidecar exists ────────
    // Python's pymeshlab writes output_path + ".mtl" instead of replacing the
    // extension.  We replicate the fix here even though we don't use pymeshlab.
    std::string wrong_mtl   = output_path + ".mtl";
    std::string correct_mtl = output_path;
    // Replace last ".obj" with ".mtl"
    size_t pos = correct_mtl.rfind(".obj");
    if (pos != std::string::npos)
        correct_mtl.replace(pos, 4, ".mtl");

    if (fs::exists(wrong_mtl)) {
        fs::rename(wrong_mtl, correct_mtl);
        std::cout << "mtl saved to " << fs::path(correct_mtl).filename().string() << "\n";
    }

    std::cout << "Saved scaled model to: " << output_path << "\n";
    std::cout << std::string(60, '=') << "\n";
}
