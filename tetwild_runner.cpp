/*
 * tetwild_runner.cpp
 *
 * Launch the FloatTetwild mesher as a child process and wait for completion.
 *
 * FloatTetwild is incorporated from source via CMake's ExternalProject /
 * FetchContent in CMakeLists.txt and built automatically.  The resulting
 * binary (FloatTetwild_bin) is placed in the build tree.
 *
 * FloatTetwild source repository:
 *   https://github.com/wildmeshing/fTetWild
 * Copyright (C) 2019 Yixin Hu <yixin.hu@nyu.edu>
 * License: Mozilla Public License 2.0
 *
 * Source: written for obj2msh
 */

#include "tetwild_runner.h"

#include <iostream>
#include <string>
#include <sstream>
#include <stdexcept>
#include <filesystem>
#include <cstdlib>
#include <array>
#include <vector>
#include <iomanip>

#if defined(_WIN32)
#  include <windows.h>
#  include <process.h>
#else
#  include <unistd.h>
#  include <sys/types.h>
#  include <sys/wait.h>
#  include <cstring>
#  include <errno.h>
#endif

namespace fs = std::filesystem;

// ─────────────────────────────────────────────────────────────────────────────
// Locate the FloatTetwild binary
//
// Search order:
//   1. OBJ2MSH_FTETWILD_BIN environment variable
//   2. Same directory as this executable (set by CMake via macro
//      OBJ2MSH_FTETWILD_BINARY_PATH)
//   3. "FloatTetwild_bin" on the system PATH
// ─────────────────────────────────────────────────────────────────────────────

// CMake will define OBJ2MSH_FTETWILD_BINARY_PATH as the build-time path;
// if not defined (e.g. manual compilation), fall back gracefully.
#ifndef OBJ2MSH_FTETWILD_BINARY_PATH
#  define OBJ2MSH_FTETWILD_BINARY_PATH ""
#endif

// Helper: resolve a path to absolute form so system() can execute it even
// when the file is in the current directory (Linux does not search '.' in PATH).
static std::string resolve_abs(const std::string& p)
{
    try {
        return fs::absolute(p).string();
    } catch (...) {
        return p;
    }
}

static std::string find_ftetwild()
{
    // 1. Env variable override
    const char* env_val = std::getenv("OBJ2MSH_FTETWILD_BIN");
    if (env_val && *env_val) {
        std::string p(env_val);
        if (fs::exists(p)) return resolve_abs(p);
    }

    // 2. Build-time path baked in by CMake
    {
        std::string btp = std::string(OBJ2MSH_FTETWILD_BINARY_PATH);
        if (!btp.empty() && fs::exists(btp)) return resolve_abs(btp);
    }

    // 3. Same directory as the running executable (most common deployment layout:
    //    FloatTetwild_bin placed beside obj2msh in the same release folder).
    //    We derive the executable's own directory from /proc/self/exe on Linux.
#if !defined(_WIN32)
    {
        char exe_buf[4096] = {};
        ssize_t len = ::readlink("/proc/self/exe", exe_buf, sizeof(exe_buf) - 1);
        if (len > 0) {
            fs::path exe_dir = fs::path(std::string(exe_buf, len)).parent_path();
            fs::path beside  = exe_dir / "FloatTetwild_bin";
            if (fs::exists(beside)) return beside.string();
        }
    }
#endif

    // 4. Current working directory explicitly (fs::exists on a bare filename
    //    checks CWD on most implementations, but system() will NOT find it
    //    there without a "./" prefix -- always resolve to absolute).
    {
        fs::path cwd_candidate = fs::current_path() / "FloatTetwild_bin";
        if (fs::exists(cwd_candidate)) return cwd_candidate.string();
    }

    // 5. Absolute system-wide install locations
    for (const std::string& p : {
            std::string("/usr/local/bin/FloatTetwild_bin"),
            std::string("/usr/bin/FloatTetwild_bin")}) {
        if (fs::exists(p)) return p;
    }

#if !defined(_WIN32)
    // 6. Fall back to searching PATH via which(1)
    std::FILE* fp = ::popen("which FloatTetwild_bin 2>/dev/null", "r");
    if (fp) {
        char buf[4096] = {};
        (void)std::fgets(buf, sizeof(buf), fp);
        ::pclose(fp);
        std::string found(buf);
        while (!found.empty() && (found.back() == '\n' || found.back() == '\r'))
            found.pop_back();
        if (!found.empty() && fs::exists(found)) return found;
    }
#endif

    throw std::runtime_error(
        "FloatTetwild binary not found.\n"
        "Searched:\n"
        "  1. OBJ2MSH_FTETWILD_BIN environment variable\n"
        "  2. Same directory as obj2msh executable\n"
        "  3. Current working directory\n"
        "  4. /usr/local/bin and /usr/bin\n"
        "  5. System PATH\n\n"
        "Place FloatTetwild_bin beside obj2msh, or set:\n"
        "  export OBJ2MSH_FTETWILD_BIN=/path/to/FloatTetwild_bin\n\n"
        "Source: https://github.com/wildmeshing/fTetWild");
}

// ─────────────────────────────────────────────────────────────────────────────
// Run a command line synchronously, returning the exit code.
// ─────────────────────────────────────────────────────────────────────────────
static int run_command(const std::string& cmd)
{
#if defined(_WIN32)
    return ::system(cmd.c_str());
#else
    int ret = ::system(cmd.c_str());
    if (WIFEXITED(ret)) return WEXITSTATUS(ret);
    return -1;
#endif
}

// ─────────────────────────────────────────────────────────────────────────────
// Pre-flight: use ldd to check whether all shared libraries that
// FloatTetwild_bin needs are present on this system.
//
// Returns an empty string if everything is satisfied, or a human-readable
// error message listing the missing libraries and the exact install command
// to fix them.
//
// Exit code 127 from system() means "command not found" but on Linux it is
// also the exit code when the dynamic linker cannot find a shared library
// (the kernel returns ELF loader errors this way).  We detect this early so
// the user gets an actionable message instead of a cryptic "exit code 127".
// ─────────────────────────────────────────────────────────────────────────────
#if !defined(_WIN32)
static std::string check_shared_libs(const std::string& binary)
{
    // ldd prints "libfoo.so.X => not found" for each missing library.
    std::string ldd_cmd = "ldd \"" + binary + "\" 2>&1";
    std::FILE*  fp      = ::popen(ldd_cmd.c_str(), "r");
    if (!fp) return "";

    std::string output;
    char buf[256];
    while (std::fgets(buf, sizeof(buf), fp))
        output += buf;
    ::pclose(fp);

    // Collect missing library names
    std::vector<std::string> missing;
    std::istringstream ss(output);
    std::string line;
    while (std::getline(ss, line)) {
        if (line.find("not found") != std::string::npos) {
            // Extract "libfoo.so.X" from lines like:
            //   "\tlibtbb.so.12 => not found"
            auto arrow = line.find("=>");
            if (arrow != std::string::npos) {
                std::string libname = line.substr(0, arrow);
                // Trim whitespace
                auto s = libname.find_first_not_of(" \t");
                auto e = libname.find_last_not_of(" \t");
                if (s != std::string::npos)
                    libname = libname.substr(s, e - s + 1);
                missing.push_back(libname);
            }
        }
    }

    if (missing.empty()) return "";

    // Build a human-readable error with distro-specific install commands.
    // Map known library SONAMES to their Ubuntu/Debian package names.
    // FloatTetwild's known runtime dependencies are listed here.
    // Source: https://github.com/wildmeshing/fTetWild
    struct LibMap { const char* soname; const char* deb_pkg; const char* rpm_pkg; };
    static const LibMap known[] = {
        { "libtbb.so.12",    "libtbb12",      "tbb"          },
        { "libgmp.so.10",    "libgmp10",      "gmp"          },
        { "libmpfr.so.6",    "libmpfr6",      "mpfr"         },
        { "libgomp.so.1",    "libgomp1",      "libgomp"      },
        { "libgcc_s.so.1",   "libgcc-s1",     "libgcc"       },
        { "libstdc++.so.6",  "libstdc++6",    "libstdc++"    },
        { "libz.so.1",       "zlib1g",        "zlib"         },
    };

    std::vector<std::string> deb_pkgs, rpm_pkgs;
    for (const auto& m : missing) {
        bool found_map = false;
        for (const auto& k : known) {
            if (m.find(k.soname) != std::string::npos) {
                deb_pkgs.push_back(k.deb_pkg);
                rpm_pkgs.push_back(k.rpm_pkg);
                found_map = true;
                break;
            }
        }
        if (!found_map) {
            // Unknown library — include the raw soname so the user can search
            deb_pkgs.push_back(m + " (search: apt-file search " + m + ")");
            rpm_pkgs.push_back(m + " (search: dnf provides " + m + ")");
        }
    }

    std::string msg;
    msg += "\n";
    msg += "FloatTetwild_bin is missing required shared libraries:\n";
    for (const auto& lib : missing)
        msg += "  " + lib + "\n";
    msg += "\n";
    msg += "Install them with one of these commands:\n";
    msg += "\n";

    // Ubuntu / Debian
    msg += "  Ubuntu 22.04 / Debian:\n";
    msg += "    sudo apt install";
    for (const auto& p : deb_pkgs) msg += " " + p;
    msg += "\n";
    msg += "    (If libtbb12 is not found, first enable the universe repo:\n";
    msg += "     sudo add-apt-repository universe && sudo apt update)\n";
    msg += "\n";

    // Fedora / RHEL
    msg += "  Fedora / RHEL / Rocky Linux:\n";
    msg += "    sudo dnf install";
    for (const auto& p : rpm_pkgs) msg += " " + p;
    msg += "\n";
    msg += "\n";

    // Arch
    msg += "  Arch Linux:\n";
    msg += "    sudo pacman -S onetbb";
    if (missing.size() > 1) msg += " gmp mpfr";
    msg += "\n";

    return msg;
}
#endif

// ─────────────────────────────────────────────────────────────────────────────
// Public API
// ─────────────────────────────────────────────────────────────────────────────
void run_tetwild(const std::string& input_stl,
                 const std::string& output_msh,
                 double             epsr)
{
    std::string ftet_bin = find_ftetwild();

    std::cout << "============================================================\n";
    std::cout << "STEP 3: RUNNING FTETWILD (Generating MSH v4.1)\n";
    std::cout << "============================================================\n";
    std::cout << "[INFO] FloatTetwild binary : " << ftet_bin << "\n";
    std::cout << "[INFO] Input               : " << input_stl << "\n";
    std::cout << "[INFO] Output (v4.1)       : " << output_msh << "\n";
    std::cout << "[INFO] epsr                : " << std::setprecision(8) << epsr << "\n\n";

    // ── Pre-flight shared library check ──────────────────────────────────────
    // Run ldd on the FloatTetwild binary before attempting to execute it.
    // This catches missing runtime libraries (e.g. libtbb.so.12) immediately
    // and prints a precise install command rather than failing with exit 127.
#if !defined(_WIN32)
    {
        std::string lib_err = check_shared_libs(ftet_bin);
        if (!lib_err.empty()) {
            throw std::runtime_error(lib_err);
        }
        std::cout << "[INFO] Shared library check : OK\n\n";
    }
#endif

    // Build the command string
    // FloatTetwild CLI flags (from its main.cpp):
    //   -i <input>  -o <output>  --epsr <value>
    std::ostringstream cmd;
    cmd << "\"" << ftet_bin << "\""
        << " -i \""  << input_stl  << "\""
        << " -o \""  << output_msh << "\""
        << " --epsr " << std::setprecision(10) << epsr;

    std::string cmd_str = cmd.str();
    std::cout << "[CMD] " << cmd_str << "\n\n";

    int rc = run_command(cmd_str);
    if (rc != 0) {
        // rc == 127 after the lib check passed means genuine execution failure
        std::string err =
            "FloatTetwild failed with exit code " + std::to_string(rc) + "\n"
            "  Command: " + cmd_str + "\n";
        if (rc == 127) {
            err += "  Exit code 127 usually means a shared library is still missing.\n"
                   "  Run:  ldd \"" + ftet_bin + "\"\n"
                   "  to see which libraries are not found.\n";
        } else {
            err += "  Check the FloatTetwild output above for details.\n"
                   "  Common causes: non-manifold mesh, insufficient RAM, very small epsr.\n";
        }
        throw std::runtime_error(err);
    }

    // fTetWild appends "_0_.msh" to the output stem when given a plain stem.
    // Check if we need to rename.
    if (!fs::exists(output_msh)) {
        // Try the _0_.msh variant
        std::string stem = output_msh;
        if (stem.size() > 4 && stem.substr(stem.size()-4) == ".msh")
            stem = stem.substr(0, stem.size()-4);
        std::string alt = stem + "_0_.msh";
        if (fs::exists(alt)) {
            fs::rename(alt, output_msh);
            std::cout << "[INFO] Renamed " << alt << " → " << output_msh << "\n";
        } else {
            throw std::runtime_error(
                "FloatTetwild did not produce the expected output: " + output_msh);
        }
    }

    std::cout << "\n[INFO] FloatTetwild complete: " << output_msh << "\n";
}
