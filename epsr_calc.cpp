/*
 * epsr_calc.cpp
 *
 * Compute fTetWild's epsr parameter from the user's smallest-feature input.
 * Translated from calculate_epsr_from_feature() in
 * master_converter_script_mm2espr.py.
 *
 * Strategy (mirroring the Python logic):
 *   epsr = smallest_feature_mm / bbox_diagonal
 *
 * This ensures epsr is always relative to the bounding-box diagonal, which
 * is what fTetWild uses internally.
 *
 * Source: translated from Python original for obj2msh
 */

#include "epsr_calc.h"

#include <fstream>
#include <sstream>
#include <string>
#include <limits>
#include <stdexcept>
#include <cmath>
#include <cstdint>

// ─────────────────────────────────────────────────────────────────────────────
// Minimal STL reader – only needs vertex coordinates
// ─────────────────────────────────────────────────────────────────────────────
static void stl_bbox(const std::string& path,
                     double& xmin, double& ymin, double& zmin,
                     double& xmax, double& ymax, double& zmax)
{
    xmin = ymin = zmin =  std::numeric_limits<double>::max();
    xmax = ymax = zmax = -std::numeric_limits<double>::max();

    std::ifstream fin(path);
    if (!fin.is_open())
        throw std::runtime_error("epsr_calc: cannot open STL: " + path);

    // Detect ASCII vs binary
    std::string header_word;
    fin >> header_word;
    fin.close();

    if (header_word == "solid") {
        // ASCII STL
        std::ifstream fa(path);
        std::string tok;
        while (fa >> tok) {
            if (tok == "vertex") {
                double x, y, z;
                fa >> x >> y >> z;
                if (x < xmin) xmin = x; if (x > xmax) xmax = x;
                if (y < ymin) ymin = y; if (y > ymax) ymax = y;
                if (z < zmin) zmin = z; if (z > zmax) zmax = z;
            }
        }
    } else {
        // Binary STL: 80-byte header, 4-byte triangle count, 50-byte/tri records
        std::ifstream fb(path, std::ios::binary);
        char header[80];
        fb.read(header, 80);
        uint32_t ntri = 0;
        fb.read(reinterpret_cast<char*>(&ntri), 4);
        for (uint32_t i = 0; i < ntri; ++i) {
            float n[3], v[9];
            uint16_t attr;
            fb.read(reinterpret_cast<char*>(n), 12);
            fb.read(reinterpret_cast<char*>(v), 36);
            fb.read(reinterpret_cast<char*>(&attr), 2);
            for (int k = 0; k < 3; ++k) {
                double x = v[k*3], y = v[k*3+1], z = v[k*3+2];
                if (x < xmin) xmin = x; if (x > xmax) xmax = x;
                if (y < ymin) ymin = y; if (y > ymax) ymax = y;
                if (z < zmin) zmin = z; if (z > zmax) zmax = z;
            }
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
double calculate_epsr_from_feature(const std::string& mesh_path,
                                   double             smallest_feature_mm,
                                   double*            out_bbox_diag,
                                   double*            out_pct_display)
{
    double xmin, ymin, zmin, xmax, ymax, zmax;
    stl_bbox(mesh_path, xmin, ymin, zmin, xmax, ymax, zmax);

    double dx = xmax - xmin;
    double dy = ymax - ymin;
    double dz = zmax - zmin;
    double bbox_diag = std::sqrt(dx*dx + dy*dy + dz*dz);

    // epsr = feature_size / diagonal  (dimensionless fraction)
    double epsr = smallest_feature_mm / bbox_diag;
    double pct  = epsr * 100.0;

    if (out_bbox_diag)   *out_bbox_diag   = bbox_diag;
    if (out_pct_display) *out_pct_display  = pct;

    return epsr;
}
