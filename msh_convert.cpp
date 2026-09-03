/*
 * msh_convert.cpp
 *
 * Convert fTetWild's Gmsh MSH output to a clean ASCII MSH v2.2 file with
 * physical tags, suitable for SOFA Framework and other FEM solvers.
 *
 * Gmsh MSH v2.2 format reference:
 *   https://gmsh.info/doc/texinfo/gmsh.html#MSH-file-format-version-2
 * Gmsh MSH v4.1 format reference:
 *   https://gmsh.info/doc/texinfo/gmsh.html#MSH-file-format
 * fTetWild source (MeshIO):
 *   https://github.com/wildmeshing/fTetWild
 *
 * Source: translated from Python original for obj2msh
 *
 * ── Format detection notes ────────────────────────────────────────────────────
 * fTetWild writes MSH v2.2 in BINARY mode (file-type flag = 1 in $MeshFormat).
 * The $MeshFormat header line is:
 *
 *   2.2 0 8   → ASCII,  8-byte doubles
 *   2.2 1 8   → Binary, 8-byte doubles  ← fTetWild default
 *
 * In binary mode the $Nodes and $Elements section data is raw packed binary:
 *   Nodes:    int32(id)  double(x)  double(y)  double(z)  per node
 *   Elements: int32(type) int32(count) int32(ntags) [tags...] [node_ids...] per block
 *
 * A text-mode getline() parser will slurp the entire binary blob as one giant
 * "line" (no \n bytes in float64 data) and never see $Nodes/$Elements sections.
 * This produces 0 nodes and 0 tets.
 *
 * Fix: open the file in binary mode (ios::binary), read $MeshFormat to detect
 * the flag, then dispatch to the correct ASCII or binary section reader.
 * ─────────────────────────────────────────────────────────────────────────────
 */

#include "msh_convert.h"

#include <fstream>
#include <sstream>
#include <iostream>
#include <string>
#include <vector>
#include <stdexcept>
#include <iomanip>
#include <cstring>
#include <cstdint>

// ─────────────────────────────────────────────────────────────────────────────
// Internal structures
// ─────────────────────────────────────────────────────────────────────────────
struct Node { double x = 0, y = 0, z = 0; };
struct Tet  { int n[4]; };

// ─────────────────────────────────────────────────────────────────────────────
// Read a newline-terminated text line from a binary-mode ifstream.
// Handles both \n and \r\n. Returns false on EOF.
// ─────────────────────────────────────────────────────────────────────────────
static bool read_line(std::ifstream& f, std::string& out)
{
    out.clear();
    char c;
    while (f.get(c)) {
        if (c == '\n') return true;
        if (c != '\r') out += c;
    }
    return !out.empty();
}

// ─────────────────────────────────────────────────────────────────────────────
// Portable little-endian binary readers
// ─────────────────────────────────────────────────────────────────────────────
static int32_t read_i32(std::ifstream& f)
{
    char buf[4];
    f.read(buf, 4);
    int32_t v;
    std::memcpy(&v, buf, 4);
    return v;
}

static double read_f64(std::ifstream& f)
{
    char buf[8];
    f.read(buf, 8);
    double v;
    std::memcpy(&v, buf, 8);
    return v;
}

// ─────────────────────────────────────────────────────────────────────────────
// Parse Binary MSH v2.2
//
// fTetWild binary format per section:
//
// $Nodes
// <num_nodes: ascii int>\n
// [for each node: int32(id) double(x) double(y) double(z)]   -- packed, no delimiters
// \n$EndNodes\n
//
// $Elements
// <num_elements: ascii int>\n
// [element blocks, each block:
//   int32(elem_type) int32(num_elems_of_this_type) int32(num_tags)
//   [for each elem: [tag int32 x ntags] [node_id int32 x nodes_per_elem] ]
// ]
// \n$EndElements\n
// ─────────────────────────────────────────────────────────────────────────────
static void parse_binary_v22(std::ifstream& fin,
                              std::vector<Node>& nodes,
                              std::vector<Tet>&  tets)
{
    // Node count: nodes_per_element per tet type
    // type 4 = tetrahedron (4 nodes)
    static const int nodes_per_elem[] = {
        0,  // 0 unused
        2,  // 1 line
        3,  // 2 triangle
        4,  // 3 quad
        4,  // 4 tet  ← we want this
        8,  // 5 hex
        6,  // 6 prism
        5,  // 7 pyramid
        3,  // 8 line3
        6,  // 9 tri6
        9,  // 10 quad9
        10, // 11 tet10
        27, // 12 hex27
        18, // 13 pri18
        14, // 14 pyr14
        1,  // 15 point
    };

    int max_node_id = 0;
    std::vector<std::pair<int,Node>> node_list;

    std::string line;
    while (read_line(fin, line)) {

        // ── $Nodes ────────────────────────────────────────────────────────────
        if (line == "$Nodes") {
            std::string count_line;
            read_line(fin, count_line);
            int nn = std::stoi(count_line);

            // Packed binary: [int32 id, f64 x, f64 y, f64 z] × nn
            for (int i = 0; i < nn; ++i) {
                int32_t id = read_i32(fin);
                double x   = read_f64(fin);
                double y   = read_f64(fin);
                double z   = read_f64(fin);
                Node nd{x, y, z};
                node_list.push_back({(int)id, nd});
                if ((int)id > max_node_id) max_node_id = (int)id;
            }
            // After the binary blob, fTetWild writes a bare \n then $EndNodes
            // read_line already consumed what it could; just skip to $EndNodes
            // (read_line will get "$EndNodes" on the next call)
        }

        // ── $Elements ─────────────────────────────────────────────────────────
        else if (line == "$Elements") {
            std::string count_line;
            read_line(fin, count_line);
            int ne_total = std::stoi(count_line);

            // Binary elements are grouped into type-blocks:
            // [int32 elem_type, int32 count, int32 ntags]
            // then for each elem: [tag × ntags] [node_id × nodes_in_type]
            int elems_read = 0;
            while (elems_read < ne_total) {
                int32_t etype  = read_i32(fin);
                int32_t ecount = read_i32(fin);
                int32_t ntags  = read_i32(fin);

                int npe = (etype >= 1 && etype <= 15) ? nodes_per_elem[etype] : 0;

                for (int i = 0; i < ecount; ++i) {
                    // Binary MSH v2.2 per-element record:
                    // int32(elem_id)  int32×ntags(tags)  int32×npe(node_ids)
                    // The element ID is part of the binary record -- must be read
                    // and discarded, or the stream goes out of sync for all
                    // subsequent elements and sections.
                    // Gmsh MSH2 binary spec:
                    //   https://gmsh.info/doc/texinfo/gmsh.html#MSH-file-format-version-2
                    (void)read_i32(fin);  // element ID -- discard

                    // Tags
                    std::vector<int32_t> tags(ntags);
                    for (int t = 0; t < ntags; ++t) tags[t] = read_i32(fin);

                    // Node ids
                    std::vector<int32_t> nids(npe);
                    for (int k = 0; k < npe; ++k) nids[k] = read_i32(fin);

                    if (etype == 4 && npe == 4) {
                        Tet tet;
                        for (int k = 0; k < 4; ++k) tet.n[k] = (int)nids[k] - 1;
                        tets.push_back(tet);
                    }
                }
                elems_read += ecount;
            }
        }
    }

    // Build flat node array
    if (max_node_id > 0) {
        nodes.resize(max_node_id);
        for (auto& [id, nd] : node_list)
            if (id >= 1 && id <= max_node_id)
                nodes[id - 1] = nd;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Parse ASCII MSH v2.2 (all $Nodes/$Elements blocks in the file)
//
// fTetWild ASCII mode writes two block pairs: surface first, volume second.
// We scan ALL blocks and accumulate nodes/tets from every one.
// ─────────────────────────────────────────────────────────────────────────────
static void parse_ascii_v22(std::ifstream& fin,
                             std::vector<Node>& nodes,
                             std::vector<Tet>&  tets)
{
    int max_node_id = 0;
    std::vector<std::pair<int,Node>> node_list;

    std::string line;
    while (read_line(fin, line)) {

        if (line == "$Nodes") {
            std::string count_line;
            read_line(fin, count_line);
            int nn = std::stoi(count_line);

            for (int i = 0; i < nn; ++i) {
                std::string nl;
                if (!read_line(fin, nl)) break;
                if (nl == "$EndNodes") break;
                std::istringstream ss(nl);
                int id; Node nd;
                if (ss >> id >> nd.x >> nd.y >> nd.z) {
                    node_list.push_back({id, nd});
                    if (id > max_node_id) max_node_id = id;
                }
            }

        } else if (line == "$Elements") {
            std::string count_line;
            read_line(fin, count_line);
            int ne = std::stoi(count_line);

            for (int i = 0; i < ne; ++i) {
                std::string el;
                if (!read_line(fin, el)) break;
                if (el == "$EndElements") break;
                std::istringstream ss(el);
                int id, type, ntags;
                if (!(ss >> id >> type >> ntags)) continue;
                for (int t = 0; t < ntags; ++t) { int d; ss >> d; }
                if (type == 4) {
                    Tet tet;
                    if (ss >> tet.n[0] >> tet.n[1] >> tet.n[2] >> tet.n[3]) {
                        for (int k = 0; k < 4; ++k) tet.n[k]--;
                        tets.push_back(tet);
                    }
                }
            }
        }
    }

    if (max_node_id > 0) {
        nodes.resize(max_node_id);
        for (auto& [id, nd] : node_list)
            if (id >= 1 && id <= max_node_id)
                nodes[id - 1] = nd;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Parse ASCII MSH v4.1
// ─────────────────────────────────────────────────────────────────────────────
static void parse_ascii_v41(std::ifstream& fin,
                             std::vector<Node>& nodes,
                             std::vector<Tet>&  tets)
{
    std::string line;
    while (read_line(fin, line)) {
        if (line == "$Nodes") {
            std::string hdr; read_line(fin, hdr);
            std::istringstream hss(hdr);
            int n_blocks, total_nodes, min_tag, max_tag;
            hss >> n_blocks >> total_nodes >> min_tag >> max_tag;
            nodes.resize(max_tag);

            for (int b = 0; b < n_blocks; ++b) {
                std::string bh; read_line(fin, bh);
                std::istringstream bss(bh);
                int edim, etag, param, n_in;
                bss >> edim >> etag >> param >> n_in;
                std::vector<int> tags(n_in);
                for (int i = 0; i < n_in; ++i) {
                    std::string tl; read_line(fin, tl);
                    tags[i] = std::stoi(tl);
                }
                for (int i = 0; i < n_in; ++i) {
                    std::string cl; read_line(fin, cl);
                    std::istringstream cs(cl);
                    int idx = tags[i] - 1;
                    if (idx >= 0 && idx < (int)nodes.size())
                        cs >> nodes[idx].x >> nodes[idx].y >> nodes[idx].z;
                }
            }
        } else if (line == "$Elements") {
            std::string hdr; read_line(fin, hdr);
            std::istringstream hss(hdr);
            int n_blocks, total_el, min_tag, max_tag;
            hss >> n_blocks >> total_el >> min_tag >> max_tag;

            for (int b = 0; b < n_blocks; ++b) {
                std::string bh; read_line(fin, bh);
                std::istringstream bss(bh);
                int edim, etag, etype, n_in;
                bss >> edim >> etag >> etype >> n_in;
                for (int i = 0; i < n_in; ++i) {
                    std::string el; read_line(fin, el);
                    std::istringstream ss(el);
                    int etag2; ss >> etag2;
                    if (etype == 4) {
                        Tet tet;
                        if (ss >> tet.n[0] >> tet.n[1] >> tet.n[2] >> tet.n[3]) {
                            for (int k=0;k<4;++k) tet.n[k]--;
                            tets.push_back(tet);
                        }
                    }
                }
            }
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Write clean ASCII MSH v2.2
// ─────────────────────────────────────────────────────────────────────────────
static void write_msh_v22(const std::string&       output_file,
                           const std::vector<Node>& nodes,
                           const std::vector<Tet>&  tets,
                           int                      physical_tag)
{
    std::ofstream fout(output_file);
    if (!fout.is_open())
        throw std::runtime_error("convert_msh_to_v22: cannot write: " + output_file);

    fout.precision(17);
    fout << std::scientific;

    fout << "$MeshFormat\n2.2 0 8\n$EndMeshFormat\n";
    fout << "$PhysicalNames\n1\n3 " << physical_tag << " \"Volume\"\n$EndPhysicalNames\n";

    fout << "$Nodes\n" << nodes.size() << "\n";
    for (int i = 0; i < (int)nodes.size(); ++i)
        fout << (i+1) << " " << nodes[i].x << " " << nodes[i].y << " " << nodes[i].z << "\n";
    fout << "$EndNodes\n";

    fout << "$Elements\n" << tets.size() << "\n";
    for (int i = 0; i < (int)tets.size(); ++i)
        fout << (i+1) << " 4 2 " << physical_tag << " " << physical_tag
             << " " << (tets[i].n[0]+1) << " " << (tets[i].n[1]+1)
             << " " << (tets[i].n[2]+1) << " " << (tets[i].n[3]+1) << "\n";
    fout << "$EndElements\n";
}

// ─────────────────────────────────────────────────────────────────────────────
// Read $MeshFormat header: returns version string and sets is_binary flag
// ─────────────────────────────────────────────────────────────────────────────
static std::string read_mesh_format(std::ifstream& fin, bool& is_binary)
{
    is_binary = false;
    std::string line;
    while (read_line(fin, line)) {
        if (line == "$MeshFormat") {
            std::string fmt;
            read_line(fin, fmt);
            std::istringstream ss(fmt);
            std::string ver;
            int filetype = 0;
            ss >> ver >> filetype;
            is_binary = (filetype == 1);

            if (is_binary) {
                // Binary MSH v2.2 writes a 4-byte endian marker (int32 value=1)
                // followed by a newline, then $EndMeshFormat.
                // Consume the endian-marker line so subsequent reads start
                // correctly at the first $Nodes or $Elements tag.
                // Ref: https://gmsh.info/doc/texinfo/gmsh.html#MSH-file-format-version-2
                std::string endian_line;
                read_line(fin, endian_line);  // 4 binary bytes -> ignored
                // $EndMeshFormat will be consumed by the outer while loop
            }
            return ver;
        }
    }
    return "unknown";
}

// ─────────────────────────────────────────────────────────────────────────────
// Public API
// ─────────────────────────────────────────────────────────────────────────────
void convert_msh_to_v22(const std::string& input_file,
                        const std::string& output_file,
                        int                physical_tag)
{
    std::cout << "\n" << std::string(60, '=') << "\n";
    std::cout << "STEP 4: CONVERTING TO ASCII MSH v2.2 FORMAT\n";
    std::cout << std::string(60, '=') << "\n";

    // Always open in binary mode so we control line-ending handling ourselves
    // and can read packed binary data when the format requires it.
    std::ifstream fin(input_file, std::ios::binary);
    if (!fin.is_open())
        throw std::runtime_error("convert_msh_to_v22: cannot open: " + input_file);

    bool        is_binary = false;
    std::string ver       = read_mesh_format(fin, is_binary);

    std::cout << "Input MSH version : " << ver << "\n";
    std::cout << "Encoding          : " << (is_binary ? "Binary" : "ASCII") << "\n";

    std::vector<Node> nodes;
    std::vector<Tet>  tets;

    if (ver == "4.1" || ver == "4") {
        std::cout << "Parser            : MSH v4.1 ASCII\n";
        parse_ascii_v41(fin, nodes, tets);
    } else if (is_binary) {
        std::cout << "Parser            : MSH v2.2 Binary (fTetWild default)\n";
        parse_binary_v22(fin, nodes, tets);
    } else {
        std::cout << "Parser            : MSH v2.2 ASCII (multi-block)\n";
        parse_ascii_v22(fin, nodes, tets);
    }
    fin.close();

    std::cout << "\nMesh statistics:\n";
    std::cout << "  Nodes      : " << nodes.size() << "\n";
    std::cout << "  Tetrahedra : " << tets.size()  << "\n";

    if (tets.empty()) {
        throw std::runtime_error(
            "convert_msh_to_v22: no tetrahedral elements found in: " + input_file + "\n"
            "  Version: " + ver + "  Binary: " + (is_binary ? "yes" : "no") + "\n"
            "  Inspect the file:  head -20 \"" + input_file + "\"");
    }

    // Warn on out-of-range node references
    int bad = 0;
    for (const auto& t : tets)
        for (int k = 0; k < 4; ++k)
            if (t.n[k] < 0 || t.n[k] >= (int)nodes.size()) ++bad;
    if (bad > 0)
        std::cout << "[WARNING] " << bad << " out-of-range node refs — mesh may be corrupt.\n";

    write_msh_v22(output_file, nodes, tets, physical_tag);

    std::cout << "\nSUCCESS\n";
    std::cout << "  Tetrahedra  : " << tets.size()  << "\n";
    std::cout << "  Physical ID : " << physical_tag << "\n";
    std::cout << "  Format      : MSH v2.2 ASCII (SOFA-compatible)\n";
    std::cout << "  Output      : " << output_file  << "\n";
    std::cout << std::string(60, '=') << "\n\n";
}
