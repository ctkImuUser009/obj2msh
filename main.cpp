/*
 * obj2msh - OBJ to MSH (SOFA-compatible v2.2) pipeline tool
 *
 * Translates the Python pipeline from master_converter_script_mm2espr.py to
 * native C++. Incorporates FloatTetwild for volumetric meshing, built from
 * source:
 *   https://github.com/wildmeshing/fTetWild
 *
 * Pipeline stages:
 *   1. Scale  - uniform scaling of the OBJ mesh (e.g. metres → mm)
 *   2. Repair - close holes + Laplacian smoothing, export as STL
 *   3. epsr   - auto-calculate fTetWild envelope from smallest feature
 *   4. Tetwild - run FloatTetwild to generate volumetric MSH v4.1
 *   5. Convert - rewrite MSH header/tags to ASCII MSH v2.2 (SOFA format)
 *   6. Verify  - sanity-check the final .msh file
 *
 * Authors: translated from Python original by Claude Sonnet 4.6
 * License: same as fTetWild (MPL 2.0) for the tetwild integration parts
 */

#include "file_picker.h"
#include "mesh_scale.h"
#include "mesh_repair.h"
#include "epsr_calc.h"
#include "tetwild_runner.h"
#include "msh_convert.h"
#include "msh_verify.h"
#include "utils.h"

#include <iostream>
#include <string>
#include <sstream>
#include <filesystem>
#include <stdexcept>
#include <cstdlib>

namespace fs = std::filesystem;

// ─────────────────────────────────────────────────────────────────────────────
// Hardcoded pipeline settings (not exposed to the end user)
// ─────────────────────────────────────────────────────────────────────────────
static constexpr int  PHYSICAL_ID   = 1;      // material tag written into MSH
static constexpr bool CLEANUP       = true;   // remove intermediate files on success
static constexpr bool VERIFY_OUTPUT = true;   // run integrity check on final .msh

// ─────────────────────────────────────────────────────────────────────────────
// helpers
// ─────────────────────────────────────────────────────────────────────────────
static void print_banner()
{
    std::cout << "\n";
    std::cout << "============================================================\n";
    std::cout << "  obj2msh  -  OBJ → SOFA-ready MSH v2.2 Pipeline\n";
    std::cout << "  (incorporates FloatTetwild: github.com/wildmeshing/fTetWild)\n";
    std::cout << "============================================================\n\n";
}

// ─────────────────────────────────────────────────────────────────────────────
// Main entry point
// ─────────────────────────────────────────────────────────────────────────────
int main(int /*argc*/, char** /*argv*/)
{
    print_banner();

    // ── Step A: pick input file ──────────────────────────────────────────────
    std::string input_obj;
    try {
        input_obj = pick_obj_file();          // opens ncurses / zenity / kdialog
    } catch (const std::exception& ex) {
        std::cerr << "[ERROR] File selection failed: " << ex.what() << "\n";
        return EXIT_FAILURE;
    }

    if (input_obj.empty()) {
        std::cerr << "[ERROR] No file selected. Aborting.\n";
        return EXIT_FAILURE;
    }

    if (!fs::exists(input_obj)) {
        std::cerr << "[ERROR] Selected file does not exist: " << input_obj << "\n";
        return EXIT_FAILURE;
    }

    std::cout << "[INFO] Selected: " << input_obj << "\n\n";

    // ── Step B: create output folder ─────────────────────────────────────────
    fs::path obj_path(input_obj);
    fs::path out_dir  = obj_path.parent_path() / "mesh_converted";

    if (fs::exists(out_dir)) {
        std::cout << "[INFO] Output folder exists, reusing: " << out_dir.string() << "\n";
    } else {
        fs::create_directories(out_dir);
        std::cout << "[INFO] Created output folder: " << out_dir.string() << "\n";
    }

    // ── Step C: collect user parameters ──────────────────────────────────────
    std::cout << "\n";
    std::cout << "Enter <output_filename_without_space.msh> <obj_scale_factor_into_mm>"
                 " <smallest_feature_to_preserve_in_mm> (space-separated):\n> ";

    std::string line;
    if (!std::getline(std::cin, line)) {
        std::cerr << "[ERROR] Could not read input.\n";
        return EXIT_FAILURE;
    }

    std::string out_filename;
    double      scale_factor      = 0.0;
    double      smallest_feature  = 0.0;

    {
        std::istringstream ss(line);
        if (!(ss >> out_filename >> scale_factor >> smallest_feature)) {
            std::cerr << "[ERROR] Could not parse three arguments from: \"" << line << "\"\n";
            std::cerr << "  Expected: <output.msh> <scale> <feature_mm>\n";
            return EXIT_FAILURE;
        }
    }

    if (scale_factor <= 0.0) {
        std::cerr << "[ERROR] scale_factor must be > 0 (got " << scale_factor << ")\n";
        return EXIT_FAILURE;
    }
    if (smallest_feature <= 0.0) {
        std::cerr << "[ERROR] smallest_feature_mm must be > 0 (got " << smallest_feature << ")\n";
        return EXIT_FAILURE;
    }
    // Ensure output filename ends with .msh
    if (out_filename.size() < 4 ||
        out_filename.substr(out_filename.size() - 4) != ".msh")
    {
        out_filename += ".msh";
    }

    std::string stem = obj_path.stem().string();   // base name without extension

    // Intermediate paths – all inside mesh_converted/
    std::string temp_scaled   = (out_dir / (stem + "_scaled.obj")).string();
    std::string temp_repaired = (out_dir / (stem + "_repaired.stl")).string();
    std::string msh_v41_path  = (out_dir / (stem + "_v41.msh")).string();
    std::string final_msh     = (out_dir / out_filename).string();

    std::cout << "\n";
    std::cout << "[INFO] Output file   : " << final_msh    << "\n";
    std::cout << "[INFO] Scale factor  : " << scale_factor << "\n";
    std::cout << "[INFO] Smallest feat : " << smallest_feature << " mm\n";
    std::cout << "[INFO] Physical ID   : " << PHYSICAL_ID  << " (fixed)\n";
    std::cout << "[INFO] Cleanup       : " << (CLEANUP       ? "yes" : "no") << " (fixed)\n";
    std::cout << "[INFO] Verify output : " << (VERIFY_OUTPUT ? "yes" : "no") << " (fixed)\n\n";

    // ── Pipeline ─────────────────────────────────────────────────────────────
    try {
        // 1. Scale
        scale_model(input_obj, temp_scaled, scale_factor);

        // 2. Repair
        repair_mesh(temp_scaled, temp_repaired);

        // 2.5 Calculate epsr
        double epsr_used = 0.0;
        {
            double bbox_diag     = 0.0;
            double bbox_pct      = 0.0;
            epsr_used = calculate_epsr_from_feature(
                            temp_repaired, smallest_feature, &bbox_diag, &bbox_pct);

            std::cout << "============================================================\n";
            std::cout << "STEP 2.5: EPSR CALCULATION\n";
            std::cout << "============================================================\n";
            std::cout << "Smallest feature to preserve : " << smallest_feature << " mm\n";
            std::cout << "Bounding box diagonal        : " << bbox_diag << " mm\n";
            std::cout << "Auto-calculated epsr         : " << epsr_used << "\n";
            std::cout << "  (= " << bbox_pct << "% of bounding box diagonal)\n";
            std::cout << "  (Ensures both feature preservation and fTetWild stability)\n";
            std::cout << "============================================================\n\n";
        }

        // 3. FloatTetwild – generates MSH v4.1
        run_tetwild(temp_repaired, msh_v41_path, epsr_used);

        // 4. Convert v4.1 → ASCII MSH v2.2 with material tags
        convert_msh_to_v22(msh_v41_path, final_msh, PHYSICAL_ID);

        std::cout << "\nPIPELINE SUCCESSFUL\n";
        std::cout << "   Final SOFA-ready mesh: " << final_msh << "\n";

        // 5. Verify
        if (VERIFY_OUTPUT) {
            MshCheckResult res = check_msh_integrity(final_msh);
            if (!res.valid) {
                std::cout << "\n[WARNING] Output verification failed. The mesh may have issues.\n";
            } else if (!res.compatible) {
                std::cout << "\n[WARNING] Output is valid but may have compatibility issues.\n";
            }
        }

        // 6. Cleanup
        if (CLEANUP) {
            std::cout << "\n[CLEANUP] Removing temporary files...\n";
            for (const auto& f : {temp_scaled, temp_repaired, msh_v41_path}) {
                if (fs::exists(f)) {
                    fs::remove(f);
                    std::cout << "   Deleted: " << f << "\n";
                }
            }
            // Remove MTL sidecar if any
            std::string mtl = temp_scaled;
            mtl.replace(mtl.size() - 3, 3, "mtl");
            if (fs::exists(mtl)) {
                fs::remove(mtl);
                std::cout << "   Deleted: " << mtl << "\n";
            }
            std::cout << "[CLEANUP] Complete\n";
        }

    } catch (const std::exception& ex) {
        std::cerr << "\n[ERROR] Pipeline failed: " << ex.what() << "\n";
        // Attempt cleanup even on failure
        if (CLEANUP) {
            for (const auto& f : {temp_scaled, temp_repaired, msh_v41_path}) {
                try { if (fs::exists(f)) fs::remove(f); } catch (...) {}
            }
        }
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
