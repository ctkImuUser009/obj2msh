/*
 * mesh_repair.cpp
 *
 * Surface mesh repair: hole closing, Laplacian smoothing, and STL export.
 * Translated from repair_mesh() in master_converter_script_mm2espr.py.
 *
 * Uses libigl (header-only) + Eigen for geometry operations.
 * libigl source: https://github.com/libigl/libigl
 * Eigen source:  https://gitlab.com/libeigen/eigen
 *
 * Source: translated from Python original for obj2msh
 */

#include "mesh_repair.h"

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <array>
#include <unordered_map>
#include <unordered_set>
#include <stdexcept>
#include <cmath>
#include <algorithm>
#include <numeric>

#include <Eigen/Dense>
#include <Eigen/Sparse>

// ─────────────────────────────────────────────────────────────────────────────
// Simple OBJ loader (vertices + triangular faces only)
// ─────────────────────────────────────────────────────────────────────────────
static void load_obj(const std::string& path,
                     Eigen::MatrixXd&   V,
                     Eigen::MatrixXi&   F)
{
    std::ifstream fin(path);
    if (!fin.is_open())
        throw std::runtime_error("repair_mesh: cannot open OBJ: " + path);

    std::vector<Eigen::Vector3d> verts;
    std::vector<Eigen::Vector3i> faces;
    std::string line;
    while (std::getline(fin, line)) {
        if (line.empty()) continue;
        std::istringstream ss(line);
        std::string tok;
        ss >> tok;
        if (tok == "v") {
            double x, y, z;
            ss >> x >> y >> z;
            verts.push_back({x, y, z});
        } else if (tok == "f") {
            // Face indices: each token is v, v/vt, or v/vt/vn
            std::vector<int> ids;
            std::string fv;
            while (ss >> fv) {
                int vi = std::stoi(fv.substr(0, fv.find('/')));
                if (vi < 0) vi = (int)verts.size() + vi + 1;
                ids.push_back(vi - 1);  // 0-based
            }
            // Fan triangulation for quads / ngons
            for (int k = 1; k + 1 < (int)ids.size(); ++k)
                faces.push_back({ids[0], ids[k], ids[k + 1]});
        }
    }

    V.resize(verts.size(), 3);
    for (int i = 0; i < (int)verts.size(); ++i)
        V.row(i) = verts[i];

    F.resize(faces.size(), 3);
    for (int i = 0; i < (int)faces.size(); ++i)
        F.row(i) = faces[i];
}

// ─────────────────────────────────────────────────────────────────────────────
// Boundary edge detection: returns list of half-edges (a,b) that appear
// exactly once across all faces (boundary edges).
// ─────────────────────────────────────────────────────────────────────────────
static std::vector<std::pair<int,int>> boundary_edges(const Eigen::MatrixXi& F)
{
    // Count occurrences of each directed edge
    std::unordered_map<long long, int> edge_count;
    auto key = [](int a, int b) -> long long {
        return (long long)std::min(a,b) * 1000000LL + std::max(a,b);
    };
    for (int i = 0; i < F.rows(); ++i) {
        for (int j = 0; j < 3; ++j) {
            int a = F(i, j), b = F(i, (j+1)%3);
            edge_count[key(a,b)]++;
        }
    }
    std::vector<std::pair<int,int>> result;
    for (int i = 0; i < F.rows(); ++i) {
        for (int j = 0; j < 3; ++j) {
            int a = F(i, j), b = F(i, (j+1)%3);
            if (edge_count[key(a,b)] == 1)
                result.push_back({a, b});
        }
    }
    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// Close holes:
//   Collects boundary loops, then fans each loop into triangles from its
//   centroid (a new vertex).  Large holes (> max_hole_size edges) are skipped.
// ─────────────────────────────────────────────────────────────────────────────
static void close_holes(Eigen::MatrixXd& V,
                        Eigen::MatrixXi& F,
                        int              max_hole_size)
{
    auto bed = boundary_edges(F);
    if (bed.empty()) return;

    // Build adjacency: for each vertex on boundary, next vertex in loop
    std::unordered_map<int,int> next_v;
    for (auto& e : bed)
        next_v[e.first] = e.second;

    // Extract loops
    std::unordered_set<int> visited;
    std::vector<std::vector<int>> loops;

    for (auto& kv : next_v) {
        if (visited.count(kv.first)) continue;
        std::vector<int> loop;
        int cur = kv.first;
        while (!visited.count(cur)) {
            visited.insert(cur);
            loop.push_back(cur);
            auto it = next_v.find(cur);
            if (it == next_v.end()) break;
            cur = it->second;
        }
        if (loop.size() >= 3)
            loops.push_back(loop);
    }

    // For each loop, add a centroid vertex and fan triangles
    std::vector<Eigen::Vector3d> extra_verts;
    std::vector<Eigen::Vector3i> extra_faces;

    for (const auto& loop : loops) {
        if ((int)loop.size() > max_hole_size) continue;

        // Centroid
        Eigen::Vector3d c = Eigen::Vector3d::Zero();
        for (int vi : loop) c += V.row(vi);
        c /= (double)loop.size();

        int ci = (int)V.rows() + (int)extra_verts.size();
        extra_verts.push_back(c);

        for (int k = 0; k < (int)loop.size(); ++k) {
            int a = loop[k];
            int b = loop[(k + 1) % loop.size()];
            extra_faces.push_back({a, ci, b});
        }
    }

    if (!extra_verts.empty()) {
        int old_nv = (int)V.rows();
        V.conservativeResize(old_nv + (int)extra_verts.size(), 3);
        for (int i = 0; i < (int)extra_verts.size(); ++i)
            V.row(old_nv + i) = extra_verts[i];

        int old_nf = (int)F.rows();
        F.conservativeResize(old_nf + (int)extra_faces.size(), 3);
        for (int i = 0; i < (int)extra_faces.size(); ++i)
            F.row(old_nf + i) = extra_faces[i];
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Laplacian smoothing (cotangent weights disabled – uniform weights, matching
// the Python call: cotangentweight=False)
// ─────────────────────────────────────────────────────────────────────────────
static void laplacian_smooth(Eigen::MatrixXd& V,
                             const Eigen::MatrixXi& F,
                             int steps)
{
    int nv = (int)V.rows();
    // Build neighbour list
    std::vector<std::vector<int>> neighbours(nv);
    for (int i = 0; i < F.rows(); ++i) {
        for (int j = 0; j < 3; ++j) {
            int a = F(i, j), b = F(i, (j+1)%3);
            neighbours[a].push_back(b);
            neighbours[b].push_back(a);
        }
    }
    // Deduplicate
    for (auto& nb : neighbours) {
        std::sort(nb.begin(), nb.end());
        nb.erase(std::unique(nb.begin(), nb.end()), nb.end());
    }

    for (int s = 0; s < steps; ++s) {
        Eigen::MatrixXd V2 = V;
        for (int i = 0; i < nv; ++i) {
            if (neighbours[i].empty()) continue;
            Eigen::Vector3d avg = Eigen::Vector3d::Zero();
            for (int nb : neighbours[i]) avg += V.row(nb);
            avg /= (double)neighbours[i].size();
            V2.row(i) = avg;
        }
        V = V2;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Remove unreferenced vertices
// ─────────────────────────────────────────────────────────────────────────────
static void remove_unreferenced(Eigen::MatrixXd& V, Eigen::MatrixXi& F)
{
    std::vector<bool> used(V.rows(), false);
    for (int i = 0; i < F.rows(); ++i)
        for (int j = 0; j < 3; ++j)
            used[F(i,j)] = true;

    std::vector<int> old2new(V.rows(), -1);
    int cnt = 0;
    for (int i = 0; i < (int)V.rows(); ++i)
        if (used[i]) old2new[i] = cnt++;

    Eigen::MatrixXd V2(cnt, 3);
    for (int i = 0; i < (int)V.rows(); ++i)
        if (old2new[i] >= 0) V2.row(old2new[i]) = V.row(i);
    V = V2;

    for (int i = 0; i < F.rows(); ++i)
        for (int j = 0; j < 3; ++j)
            F(i,j) = old2new[F(i,j)];
}

// ─────────────────────────────────────────────────────────────────────────────
// Write STL (ASCII)
// ─────────────────────────────────────────────────────────────────────────────
static void write_stl(const std::string& path,
                      const Eigen::MatrixXd& V,
                      const Eigen::MatrixXi& F)
{
    std::ofstream fout(path);
    if (!fout.is_open())
        throw std::runtime_error("repair_mesh: cannot write STL: " + path);

    fout << "solid repaired\n";
    fout.precision(10);
    for (int i = 0; i < F.rows(); ++i) {
        Eigen::Vector3d v0 = V.row(F(i,0));
        Eigen::Vector3d v1 = V.row(F(i,1));
        Eigen::Vector3d v2 = V.row(F(i,2));
        Eigen::Vector3d n  = (v1 - v0).cross(v2 - v0);
        double len = n.norm();
        if (len > 1e-15) n /= len;
        fout << "  facet normal " << n[0] << " " << n[1] << " " << n[2] << "\n";
        fout << "    outer loop\n";
        fout << "      vertex " << v0[0] << " " << v0[1] << " " << v0[2] << "\n";
        fout << "      vertex " << v1[0] << " " << v1[1] << " " << v1[2] << "\n";
        fout << "      vertex " << v2[0] << " " << v2[1] << " " << v2[2] << "\n";
        fout << "    endloop\n";
        fout << "  endfacet\n";
    }
    fout << "endsolid repaired\n";
}

// ─────────────────────────────────────────────────────────────────────────────
// Count boundary loops (for the topology report)
// ─────────────────────────────────────────────────────────────────────────────
static int count_holes(const Eigen::MatrixXi& F)
{
    auto bed = boundary_edges(F);
    if (bed.empty()) return 0;
    std::unordered_map<int,int> next_v;
    for (auto& e : bed) next_v[e.first] = e.second;
    std::unordered_set<int> visited;
    int holes = 0;
    for (auto& kv : next_v) {
        if (visited.count(kv.first)) continue;
        int cur = kv.first;
        while (!visited.count(cur)) {
            visited.insert(cur);
            auto it = next_v.find(cur);
            if (it == next_v.end()) break;
            cur = it->second;
        }
        ++holes;
    }
    return holes;
}

// ─────────────────────────────────────────────────────────────────────────────
// Public API
// ─────────────────────────────────────────────────────────────────────────────
void repair_mesh(const std::string& input_file,
                 const std::string& output_file,
                 int                max_hole_size,
                 int                smooth_steps)
{
    std::cout << std::string(60, '=') << "\n";
    std::cout << "PRE-PROCESSING FOR SOLID ROD OUTPUT\n";
    std::cout << std::string(60, '=') << "\n";
    std::cout << "Loading mesh: " << input_file << "\n";

    Eigen::MatrixXd V;
    Eigen::MatrixXi F;
    load_obj(input_file, V, F);

    // 1. Close holes
    std::cout << "[1] Closing holes to ensure a water-tight volume...\n";
    close_holes(V, F, max_hole_size);

    // 2. Laplacian smoothing
    std::cout << "[2] Smoothing surface for better volumetric quality...\n";
    laplacian_smooth(V, F, smooth_steps);

    // 3. Topology report
    bool is_manifold = true;
    {
        // A simple manifold check: every edge shared by at most 2 faces
        std::unordered_map<long long, int> edge_cnt;
        auto key = [](int a, int b) -> long long {
            return (long long)std::min(a,b) * 1000000LL + std::max(a,b);
        };
        for (int i = 0; i < F.rows(); ++i)
            for (int j = 0; j < 3; ++j)
                if (++edge_cnt[key(F(i,j), F(i,(j+1)%3))] > 2) {
                    is_manifold = false; break;
                }
    }
    int remaining_holes = count_holes(F);

    std::cout << "    Status: " << V.rows() << " vertices\n";
    std::cout << "    Is Manifold: " << (is_manifold ? "true" : "false") << "\n";
    std::cout << "    Remaining Holes: " << remaining_holes << "\n";

    // 4. Remove unreferenced vertices
    remove_unreferenced(V, F);

    // 5. Save as STL
    write_stl(output_file, V, F);
    std::cout << "\nPre-processed mesh saved: " << output_file << "\n";
    std::cout << std::string(60, '=') << "\n";
}
