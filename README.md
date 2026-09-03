# obj2msh — Linux User Guide

**OBJ → SOFA-compatible MSH v2.2 pipeline tool**

Converts surface meshes (`.obj`) into volumetric tetrahedral meshes (`.msh` Gmsh v2.2 ASCII format) suitable for use in SOFA Framework and other FEM simulation environments. The pipeline automates scaling, surface repair, epsr calculation, volumetric meshing via FloatTetwild, format conversion, and integrity verification.

---

## Table of Contents

1. [What This Tool Does](#what-this-tool-does)
2. [Pipeline Overview](#pipeline-overview)
3. [Quick Start — Pre-built Binary](#quick-start--pre-built-binary)
4. [Building from Source](#building-from-source)
5. [How to Use](#how-to-use)
6. [Understanding the Parameters](#understanding-the-parameters)
7. [Output Files and Folder Structure](#output-files-and-folder-structure)
8. [How FloatTetwild Is Integrated](#how-floattetwild-is-integrated)
9. [glibc Compatibility Shim — glibc_compat.c](#glibc-compatibility-shim--glibc_compatc)
10. [Environment Variable Override](#environment-variable-override)
11. [Troubleshooting](#troubleshooting)
12. [Source File Reference](#source-file-reference)
13. [Dependency Reference](#dependency-reference)
14. [Tested Models](#tested-models)
15. [Licence Notes](#licence-notes)

---

## What This Tool Does

`obj2msh` is a native C++ translation of `master_converter_script_mm2espr.py`, a Python pipeline that originally used pymeshlab, meshio, and a separately installed FloatTetwild binary.

The C++ version:
- Requires no Python interpreter, no pymeshlab, and no pre-installed FloatTetwild
- Builds and bundles FloatTetwild automatically from its GitHub source during the CMake build
- Provides an interactive **ncurses terminal file browser** to pick your `.obj` file without typing a path
- Writes all intermediate and final files neatly inside a `mesh_converted/` folder beside your input
- The pre-built binary targets **Ubuntu 22.04 LTS (Jammy Jellyfish)** and any distro with glibc 2.35+, achieved via a small bundled compatibility shim (`src/glibc_compat.c`) — no extra installation needed

---

## Pipeline Overview

```
Your .obj file
      │
      ▼  Stage 1 — Scale
      │   Uniform scale by your factor (e.g. 1000× for metres → mm)
      │   Writes:  mesh_converted/<name>_scaled.obj
      │
      ▼  Stage 2 — Repair
      │   Close holes (fan triangulation from centroid)
      │   Laplacian smoothing (3 passes, uniform weights)
      │   Remove unreferenced vertices
      │   Writes:  mesh_converted/<name>_repaired.stl
      │
      ▼  Stage 2.5 — epsr Calculation
      │   Reads bounding-box diagonal of repaired mesh
      │   epsr = smallest_feature_mm / bbox_diagonal
      │   (dimensionless; what FloatTetwild uses internally)
      │
      ▼  Stage 3 — Volumetric Meshing (FloatTetwild)
      │   Runs FloatTetwild_bin with -i / -o / --epsr flags
      │   Writes:  mesh_converted/<name>_v41.msh  (Gmsh v4.1)
      │            mesh_converted/<name>_v41.msh_.csv  (quality metrics)
      │            mesh_converted/<name>_v41.msh_sf.obj  (simplified surface)
      │            mesh_converted/<name>_v41.msh_tracked_surface.stl  (tracked surface)
      │
      ▼  Stage 4 — Format Conversion
      │   Parses the v4.1 MSH, extracts nodes + tetrahedra
      │   Writes ASCII MSH v2.2 with physical_id=1 on all tets
      │   Writes:  mesh_converted/<your_output_name>.msh
      │
      ▼  Stage 5 — Verification
      │   Checks header version, node count, tet count
      │   Prints compatibility report
      │
      ▼  Stage 6 — Cleanup (automatic)
          Deletes _scaled.obj, _repaired.stl, _v41.msh
          Deletes FloatTetwild sidecars: _v41.msh_.csv, _v41.msh_sf.obj,
                                         _v41.msh_tracked_surface.stl
          Keeps only the final .msh
```

**Hardcoded settings** (not user-configurable, matching the original Python defaults):

| Setting | Value | Python equivalent |
|---|---|---|
| `physical_id` | 1 | `physical_id=1` |
| `cleanup` | true | `cleanup=True` |
| `verify_output` | true | `verify_output=True` |

---

## Quick Start — Pre-built Binary

The `obj2msh` ELF binary was compiled with GCC 13 on Ubuntu 24.04 and specifically tuned to run on **Ubuntu 22.04 LTS (Jammy Jellyfish)** and any distribution that provides **glibc 2.35 or newer**.

**How compatibility with Jammy is achieved:** GCC 13 normally emits calls to three symbols that are absent from glibc 2.35 — `__isoc23_strtol`, `__isoc23_strtoul` (C23 variants added in glibc 2.38), and `arc4random` (added in glibc 2.36). The source file `src/glibc_compat.c` provides its own implementations of all three, compiled directly into the binary, so the system glibc is never asked for them. Additionally, `libstdc++` and `libgcc` are statically linked in, removing any dependency on the system's C++ runtime version. The net result is that the binary only requires glibc 2.35 at runtime — exactly what Jammy ships.

### 1. Install runtime dependencies

A convenience script is included in the release archive that installs everything in one step:

```bash
chmod +x install_deps.sh
./install_deps.sh
```

This script handles the `universe` repository requirement for `libtbb12` automatically, and verifies that `FloatTetwild_bin` can find all its libraries after installation.

**What gets installed:**

| Library | Package | Needed by |
|---|---|---|
| `libncurses.so.6` | `libncurses6` | obj2msh (file browser UI) |
| `libtinfo.so.6` | `libtinfo6` | obj2msh (ncurses dependency) |
| `libtbb.so.12` | `libtbb12` *(universe repo)* | FloatTetwild_bin (threading) |
| `libgmp.so.10` | `libgmp10` | FloatTetwild_bin |
| `libmpfr.so.6` | `libmpfr6` | FloatTetwild_bin |

`libstdc++` and `libgcc` are statically linked into `obj2msh` and not needed at runtime. They are also installed by the script for `FloatTetwild_bin` on truly minimal systems.

**Manual install (if you prefer):**
```bash
# Ubuntu 22.04 — enable universe repo first for libtbb12
sudo add-apt-repository universe
sudo apt update
sudo apt install libncurses6 libtinfo6 libtbb12 libgmp10 libmpfr6

# Fedora / RHEL
sudo dnf install tbb gmp mpfr ncurses-libs

# Arch Linux
sudo pacman -S onetbb gmp mpfr ncurses
```

### 2. Make it executable

```bash
chmod +x obj2msh
```

### 3. Obtain FloatTetwild_bin

`obj2msh` is a pipeline wrapper — it calls FloatTetwild as a subprocess for the volumetric meshing stage. FloatTetwild is a separate project (MPL 2.0, by Yixin Hu et al., https://github.com/wildmeshing/fTetWild) and its binary is not bundled inside `obj2msh` itself.

**If you are the project maintainer distributing this on GitHub:** you are allowed to ship the compiled `FloatTetwild_bin` binary in your release assets or as a file in your repository, provided you include proper attribution and a copy of or link to the MPL 2.0 licence. This is the friendliest option for your users — FloatTetwild takes 30–60 minutes and several system libraries to build, which is an unreasonable burden to impose. See [Licence Notes](#licence-notes) for exactly what attribution is required.

**If you are a user who downloaded this from the project's GitHub releases:** the `FloatTetwild_bin` binary should have been included alongside `obj2msh` in the release archive. Place both files in the same folder and proceed to step 4.

If for any reason `FloatTetwild_bin` is missing from the release, you have two options:

**Option A — place `FloatTetwild_bin` in the same folder as `obj2msh` (recommended):**
```bash
# No configuration needed. The tool resolves its own executable path
# at runtime via /proc/self/exe and looks there first.
# Layout:
#   ~/your/folder/obj2msh
#   ~/your/folder/FloatTetwild_bin   <- just put it here
./obj2msh
```
This works on every user's machine regardless of where they install the folder, because the path is resolved at runtime, not hardcoded.

**Option B — set an environment variable to point to it anywhere on disk:**
```bash
export OBJ2MSH_FTETWILD_BIN=/path/to/FloatTetwild_bin
./obj2msh
```

**Option C — build everything from source** — the CMake build fetches and compiles FloatTetwild automatically. See [Building from Source](#building-from-source). Use this if you need a FloatTetwild binary built specifically for your system or CPU architecture.

### 4. Run

```bash
./obj2msh
```

---

## Building from Source

Building from source is the recommended path. CMake will **automatically download and compile FloatTetwild** from `https://github.com/wildmeshing/fTetWild` as part of the build. No manual FloatTetwild installation is needed.

> **Estimated build time:** 20–40 minutes on first build (FloatTetwild pulls geogram, libigl, spdlog, and several other libraries). Subsequent builds are instant.

### System requirements

| Requirement | Minimum version | Notes |
|---|---|---|
| GCC or Clang | GCC 9+ / Clang 10+ | C++17 required |
| CMake | 3.16+ | |
| Git | any | For cloning FloatTetwild |
| Eigen3 | 3.3+ | Header-only math library |
| GMP | any | GNU Multiple Precision (for FloatTetwild) |
| MPFR | any | GNU MPFR (for FloatTetwild) |
| ncurses | any | For the interactive file browser |
| Internet | — | Required on first build to clone FloatTetwild |

### Install dependencies

**Ubuntu / Debian:**
```bash
sudo apt update
sudo apt install -y \
    build-essential \
    cmake \
    git \
    libeigen3-dev \
    libgmp-dev \
    libmpfr-dev \
    libncurses-dev
```

**Fedora / RHEL / Rocky Linux:**
```bash
sudo dnf install -y \
    gcc-c++ \
    cmake \
    git \
    eigen3-devel \
    gmp-devel \
    mpfr-devel \
    ncurses-devel
```

**Arch Linux:**
```bash
sudo pacman -S --needed \
    base-devel \
    cmake \
    git \
    eigen \
    gmp \
    mpfr \
    ncurses
```

### Build

```bash
# Clone or place the source tree, then:
cd obj2msh
mkdir build && cd build

cmake .. -DCMAKE_BUILD_TYPE=Release

# Use all CPU cores for a faster build
make -j$(nproc)
```

CMake will print a summary at configure time:
```
=== obj2msh build configuration ===
  C++ standard   : C++17
  Eigen3         : /usr/include/eigen3
  ncurses        : TRUE
  FloatTetwild   : fetched from https://github.com/wildmeshing/fTetWild
  fTetWild bin   : /your/build/path/ftetwild_install/bin/FloatTetwild_bin
```

After a successful build, the tree looks like:
```
build/
├── obj2msh                             ← main executable
└── ftetwild_install/
    └── bin/
        └── FloatTetwild_bin            ← auto-built mesher
```

The `obj2msh` binary has the path to `FloatTetwild_bin` **baked in at compile time**, so you can run `./obj2msh` directly without any environment variables.

`src/glibc_compat.c` is automatically compiled into `obj2msh` by CMake (it is listed in `OBJ2MSH_SOURCES` in `CMakeLists.txt`). No special steps are needed — it is just another source file. If you are building on Ubuntu 22.04 itself, its stub functions compile in harmlessly and are never called.

### Optional: system-wide install

```bash
sudo cmake --install .
# Installs obj2msh and FloatTetwild_bin to /usr/local/bin/
```

---

## How to Use

### Launch

```bash
./obj2msh
# or, if installed system-wide:
obj2msh
```

### Step 1 — File Browser

The terminal will show an interactive ncurses file browser:

```
 obj2msh - Select .obj File
 Dir: /home/youruser/models
------------------------------------------------------------
 ../
 scans/                                <- directories in cyan
 femur.obj                             <- .obj files in green
 pelvis.obj
 Up/Dn=navigate  ENTER=select/open  q=quit  Dirs shown in cyan, .obj in green
```

The separator is drawn with plain ASCII dashes and the footer uses plain text keys, so the browser displays correctly on any terminal regardless of locale or UTF-8 support.

**Controls:**

| Key | Action |
|---|---|
| Arrow Up / Arrow Down | Move selection up / down |
| `Page Up` / `Page Down` | Jump by one screen |
| `Enter` | Open a directory / select a `.obj` file |
| `q` or `Esc` | Cancel and exit |

Only directories and `.obj` files are shown. Navigate into subdirectories freely.

### Step 2 — Output folder

After selecting your file, `obj2msh` automatically creates (or reuses) a `mesh_converted/` folder in the same directory as your `.obj` file:

```
[INFO] Selected: /home/youruser/models/femur.obj
[INFO] Created output folder: /home/youruser/models/mesh_converted
```

If `mesh_converted/` already exists, it is reused without being wiped.

### Step 3 — Enter parameters

You will see this prompt:

```
Enter <output_filename_without_space.msh> <obj_scale_factor_into_mm> <smallest_feature_to_preserve_in_mm> (space-separated):
>
```

Type all three values on one line, separated by spaces, then press Enter:

```
> femur_sofa.msh 1000 2.0
```

| Parameter | Example | Meaning |
|---|---|---|
| `output_filename_without_space.msh` | `femur_sofa.msh` | Name of the final output file. No spaces. The `.msh` extension is added automatically if omitted. |
| `obj_scale_factor_into_mm` | `1000` | Multiplier applied to all vertex coordinates. Use `1000` if your OBJ is in metres and you want mm. Use `1` if already in mm. |
| `smallest_feature_to_preserve_in_mm` | `2.0` | The smallest anatomical detail you want the mesh to faithfully reproduce, in mm after scaling. Smaller values → finer mesh → longer runtime. |

**Typical inputs:**

For a standard anatomical mesh in metres (e.g. exported from medical imaging software), scaling to mm with 2 mm feature preservation:
```
> my_output_mesh.msh 1000.0 2.0
```

For a mesh already in mm (e.g. from a CT scan exported at 1:1 scale), keeping fine detail at 0.25 mm:
```
> my_output_mesh.msh 1.0 0.25
```

As a rule of thumb, start with `2.0` for the feature size and reduce it only if the mesh loses important anatomical detail. Smaller values produce denser meshes and significantly longer FloatTetwild runtimes.

### Step 4 — Watch the pipeline run

The tool prints detailed progress for every stage. Typical output:

```
============================================================
MODEL SCALE REPORT: /home/youruser/models/femur.obj
============================================================
EXTENTS BEFORE SCALING:
  X: 0.0712
  Y: 0.1483
  Z: 0.0341
------------------------------------------------------------
APPLIED MULTIPLIER: 1000x
EXTENTS AFTER SCALING:
  X: 71.2000
  Y: 148.3000
  Z: 34.1000
------------------------------------------------------------

============================================================
PRE-PROCESSING FOR SOLID ROD OUTPUT
============================================================
[1] Closing holes to ensure a water-tight volume...
[2] Smoothing surface for better volumetric quality...
    Status: 14382 vertices
    Is Manifold: true
    Remaining Holes: 0

============================================================
STEP 2.5: EPSR CALCULATION
============================================================
Smallest feature to preserve : 2 mm
Bounding box diagonal        : 166.84 mm
Auto-calculated epsr         : 0.011986
  (= 1.199% of bounding box diagonal)

STEP 3: RUNNING FTETWILD (Generating MSH v4.1)...
...

STEP 4: CONVERTING TO ASCII MSH v2.2 FORMAT
  Points: 28741
  tetra:  142398

PIPELINE SUCCESSFUL
   Final SOFA-ready mesh: /home/youruser/models/mesh_converted/femur_sofa.msh

      DETAILED MESH INTEGRITY & VERSION REPORT
==================================================
Format Check:   [OK] v2.2 Confirmed (High Compatibility)
Number of Nodes: 28741
Tetrahedra:      142398
RESULT: [OK] SUCCESS (File is healthy and widely compatible)
```

---

## Understanding the Parameters

### `obj_scale_factor_into_mm`

Your OBJ file's unit system is usually unknown to the tool; you tell it here:

| Your OBJ unit | Target unit | Scale factor |
|---|---|---|
| Metres | mm | `1000` |
| Centimetres | mm | `10` |
| Inches | mm | `25.4` |
| Already mm | mm | `1` |

### `smallest_feature_to_preserve_in_mm`

This controls the **epsr** (epsilon-relative) parameter fed to FloatTetwild. The formula is:

```
epsr = smallest_feature_mm / bounding_box_diagonal_mm
```

FloatTetwild uses epsr as a surface approximation tolerance — the maximum allowed distance between the original surface and the tetrahedral mesh surface, expressed as a fraction of the bounding box diagonal.

**Practical guidance:**

| Feature size | Use case | Mesh density | Runtime |
|---|---|---|---|
| `5.0` mm | Rough structural mesh, large bodies | Coarse | Fast |
| `2.0` mm | General anatomical mesh (recommended start) | Medium | Moderate |
| `1.0` mm | Fine anatomical detail | Dense | Slower |
| `0.5` mm | Very fine detail, thin walls | Very dense | Slow |

Start with `2.0` and decrease only if the mesh loses important anatomical features.

### Hardcoded parameters (not prompted)

These mirror the Python script's recommended production settings:

- **`physical_id = 1`** — All tetrahedral elements receive material tag 1 in the MSH output, which SOFA uses to assign material properties.
- **`cleanup = true`** — Intermediate files (`_scaled.obj`, `_repaired.stl`, `_v41.msh`) and FloatTetwild sidecar files (`_v41.msh_.csv`, `_v41.msh_sf.obj`, `_v41.msh_tracked_surface.stl`) are all deleted after a successful run.
- **`verify_output = true`** — A header and geometry check is run on the final `.msh` before the tool exits.

---

## Output Files and Folder Structure

Given input `/home/user/models/femur.obj` and output name `femur_sofa.msh`:

```
/home/user/models/
├── femur.obj                                      ← your original (untouched)
└── mesh_converted/
    ├── femur_scaled.obj                           ← Stage 1 output (deleted on success)
    ├── femur_repaired.stl                         ← Stage 2 output (deleted on success)
    ├── femur_v41.msh                              ← Stage 3 output (deleted on success)
    ├── femur_v41.msh_.csv                         ← FloatTetwild sidecar (deleted on success)
    ├── femur_v41.msh_sf.obj                       ← FloatTetwild sidecar (deleted on success)
    ├── femur_v41.msh_tracked_surface.stl          ← FloatTetwild sidecar (deleted on success)
    └── femur_sofa.msh                             ← FINAL OUTPUT — load this in SOFA
```

### FloatTetwild sidecar files

FloatTetwild writes three auxiliary files alongside its main `.msh` output. These are normal — all three are deleted automatically during Stage 6 cleanup on a successful run:

| File | Contents |
|---|---|
| `*_v41.msh_.csv` | Per-element energy/quality metrics from the FloatTetwild optimisation passes. Useful for diagnosing mesh quality if the final mesh has poor-quality elements. |
| `*_v41.msh_sf.obj` | The **simplified surface** that FloatTetwild fitted to the input geometry. Inspect this in MeshLab or Blender if the final volumetric mesh has surface approximation errors. |
| `*_v41.msh_tracked_surface.stl` | The **tracked surface** mesh — FloatTetwild's internal record of which surface triangles correspond to which volume faces. Useful for debugging surface-to-volume correspondence issues in SOFA. |

These files are only visible if the pipeline fails after Stage 3 (FloatTetwild) but before Stage 6 (cleanup), for example if the format conversion step encounters an error. In that case they are preserved so you can inspect the FloatTetwild output directly.

If the tool fails mid-pipeline for any reason, all intermediate files are preserved so you can inspect what went wrong.

---

## How FloatTetwild Is Integrated

FloatTetwild (https://github.com/wildmeshing/fTetWild) is a separate open-source project by Yixin Hu et al., licensed under MPL 2.0. `obj2msh` does not incorporate FloatTetwild's source code — it invokes the FloatTetwild binary as a subprocess for the volumetric meshing stage (Stage 3).

**In release archives**, `FloatTetwild_bin` is distributed alongside `obj2msh` as a pre-built binary, with attribution in `THIRD_PARTY_LICENSES.md`. This is permitted under MPL 2.0.

> **Platform note:** The `FloatTetwild_bin` binary included in this release was compiled and tested on **Ubuntu 22.04 LTS (Jammy Jellyfish)** only. It has not been tested on other distributions. If you are on a different distro or a different Ubuntu version, the binary may or may not work — run `./install_deps.sh` first to satisfy its runtime dependencies, and if it still fails, build FloatTetwild from source using the CMake path described below.

**When building from source**, the `CMakeLists.txt` uses CMake's `ExternalProject_Add` to:

1. Clone `https://github.com/wildmeshing/fTetWild` at configure time (shallow clone, `main` branch)
2. Configure and build it as a Release build with tests disabled
3. Copy the resulting `FloatTetwild_bin` binary into `build/ftetwild_install/bin/`
4. Bake that path into `obj2msh` via the `OBJ2MSH_FTETWILD_BINARY_PATH` compile definition

At runtime, `tetwild_runner.cpp` searches for the binary in this order:

1. `OBJ2MSH_FTETWILD_BIN` environment variable
2. The baked-in compile-time path (set by CMake, used when building from source)
3. **Same directory as the `obj2msh` executable** (resolved via `/proc/self/exe`) — this is the recommended deployment layout: just place `FloatTetwild_bin` beside `obj2msh`
4. Current working directory (resolved to an absolute path)
5. `/usr/local/bin/FloatTetwild_bin` and `/usr/bin/FloatTetwild_bin`
6. System PATH via `which FloatTetwild_bin`

Every found path is resolved to its absolute form before being passed to the shell, so the binary is found correctly even when run from a different working directory. If nothing is found, the error message lists all six locations that were checked.

FloatTetwild is invoked with:
```
FloatTetwild_bin -i <repaired.stl> -o <output_v41.msh> --epsr <value>
```

FloatTetwild source and licence: https://github.com/wildmeshing/fTetWild (MPL 2.0, Copyright © 2019 Yixin Hu)

---

## glibc Compatibility Shim — glibc_compat.c

### What it is

`src/glibc_compat.c` is a small C source file in this repository that provides stub implementations of three symbols that GCC 13 emits but that are absent from glibc 2.35 (Ubuntu 22.04 Jammy):

| Symbol | Introduced in glibc | Root cause | Shim behaviour |
|---|---|---|---|
| `__isoc23_strtol` | 2.38 | GCC 13 emits this instead of `strtol` in C23 mode, triggered by `std::stoi`/`std::stol` inside the statically-linked libstdc++ | Delegates directly to `strtol()` |
| `__isoc23_strtoul` | 2.38 | Same as above for unsigned variants | Delegates directly to `strtoul()` |
| `arc4random` | 2.36 | libstdc++'s internal PRNG calls `arc4random` when available at link time | xorshift32 seeded from `/dev/urandom`, with `time()` as fallback |

These stubs are compiled into `obj2msh` itself at link time. The binary therefore never asks the system glibc for these symbols at runtime — the definitions are already inside the executable.

### Why it exists

The binary was compiled on Ubuntu 24.04 (glibc 2.38) targeting Ubuntu 22.04 (glibc 2.35). Without the shim, running the binary on Jammy would fail immediately with:

```
./obj2msh: /lib/x86_64-linux-gnu/libc.so.6: version 'GLIBC_2.38' not found
./obj2msh: /lib/x86_64-linux-gnu/libc.so.6: version 'GLIBC_2.36' not found
```

### Do you need to include it in your GitHub repository?

**Yes, absolutely.** `glibc_compat.c` is a first-party source file — entirely original code written for this project, with no copied content from glibc or any other library. Anyone who clones your repository and builds from source on GCC 13 will need it to produce a Jammy-compatible binary. It must be present in `src/` alongside the other `.cpp` files.

### Does it create any licence obligations?

**No.** The file is 100% original code. It does not reproduce, link against, or derive from any part of glibc's source. The three functions happen to share names with glibc symbols, but the implementations are independent. You own this file entirely and can licence it under whatever terms you choose for the rest of the project.

### Do you need it if you only build on Ubuntu 22.04?

**No.** If you compile on a machine that already has glibc 2.35 (i.e. any Jammy system), GCC will naturally emit `strtol`/`strtoul` and will not call `arc4random` at link time. The shim compiles harmlessly in that case — its definitions are simply never called — so it is safe to keep in the source tree unconditionally.

### GitHub repository layout

Your repository should include this file exactly as shown:

```
obj2msh/
├── CMakeLists.txt
├── install_deps.sh             ← dependency installer for users
├── src/
│   ├── main.cpp
│   ├── file_picker.cpp
│   ├── mesh_scale.cpp
│   ├── mesh_repair.cpp
│   ├── epsr_calc.cpp
│   ├── tetwild_runner.cpp
│   ├── msh_convert.cpp
│   ├── msh_verify.cpp
│   └── glibc_compat.c
├── include/
│   └── *.h
├── THIRD_PARTY_LICENSES.md     ← required for FloatTetwild redistribution (see Licence Notes)
├── README_LINUX.md
└── README_WINDOWS.md
```

For GitHub **releases**, attach a zip/tarball:

```
obj2msh_linux_jammy.zip
├── obj2msh                     ← your pipeline binary
├── FloatTetwild_bin            ← redistributed under MPL 2.0 (attribution in THIRD_PARTY_LICENSES.md)
├── install_deps.sh             ← run this first on a fresh machine
├── THIRD_PARTY_LICENSES.md
└── README_LINUX.md
```

Users run `./install_deps.sh` once after downloading, then `./obj2msh`. The script handles the `universe` repository, installs all runtime libraries for both binaries, and verifies that `FloatTetwild_bin` can load all its dependencies.

The `CMakeLists.txt` must also list `src/glibc_compat.c` in the source list for `obj2msh`. Add it to the `OBJ2MSH_SOURCES` variable:

```cmake
set(OBJ2MSH_SOURCES
    src/main.cpp
    src/file_picker.cpp
    src/mesh_scale.cpp
    src/mesh_repair.cpp
    src/epsr_calc.cpp
    src/tetwild_runner.cpp
    src/msh_convert.cpp
    src/msh_verify.cpp
    src/glibc_compat.c          # glibc 2.35 (Jammy) compatibility shim
)
```

---

## Environment Variable Override

If you have a pre-built FloatTetwild binary elsewhere on your system, you can override the path without recompiling:

```bash
export OBJ2MSH_FTETWILD_BIN=/opt/ftetwild/FloatTetwild_bin
./obj2msh
```

This takes the highest priority over all other search locations.

---

## Troubleshooting

### `error while loading shared libraries: libncurses.so.6`

Install ncurses runtime:
```bash
sudo apt install libncurses6       # Debian/Ubuntu
sudo dnf install ncurses           # Fedora
sudo pacman -S ncurses             # Arch
```

### `FloatTetwild binary not found`

The tool searched six locations and found nothing. The error message printed to the terminal lists exactly which paths were checked. The quickest fix is placing `FloatTetwild_bin` in the same folder as `obj2msh` — the tool resolves the executable's own directory at runtime via `/proc/self/exe`, so this works regardless of where on any user's machine the folder lives:

```
~/anywhere/obj2msh/
    obj2msh              <- your pipeline binary
    FloatTetwild_bin     <- place it here, no configuration needed
```

Alternatively, point to it with an environment variable:
```bash
export OBJ2MSH_FTETWILD_BIN=/path/to/FloatTetwild_bin
./obj2msh
```

Or build from source — CMake downloads and compiles FloatTetwild automatically and bakes the path into the binary.

### `FloatTetwild_bin: error while loading shared libraries: libtbb.so.12`

`FloatTetwild_bin` needs Intel TBB (Threading Building Blocks) at runtime. On Ubuntu 22.04, this library is in the `universe` repository which is not always enabled.

**Quickest fix — run the included installer script:**
```bash
./install_deps.sh
```

**Manual fix:**
```bash
sudo add-apt-repository universe
sudo apt update
sudo apt install libtbb12
```

After installation, verify `FloatTetwild_bin` can find all its libraries:
```bash
ldd FloatTetwild_bin
# Every line should show a path, not "=> not found"
```

If you see `libtbb.so.12 => not found` or similar after installing, the universe repo was not added correctly. Check with `apt-cache show libtbb12` — if it shows no output, re-run `sudo add-apt-repository universe && sudo apt update`.

**Why `obj2msh` detects this before running:** `tetwild_runner.cpp` calls `ldd` on `FloatTetwild_bin` before attempting to execute it. If any library is missing, it prints the exact `apt install` command needed and exits cleanly, rather than failing with a cryptic `exit code 127`.

### `FloatTetwild failed with exit code ...`

Common causes:
- Input mesh has severe self-intersections or non-manifold edges after repair
- epsr is too small for the mesh size (try a larger `smallest_feature_mm`, e.g. `5.0`)
- Insufficient RAM — fTetWild is memory-intensive on dense meshes (4 GB+ recommended)

Check the FloatTetwild output printed to the terminal for its own error messages.

### `convert_msh_to_v22: no tetrahedral elements found`

FloatTetwild ran but produced no interior volume. This usually means the input surface was not closed (all holes filled). Inspect `mesh_converted/<name>_repaired.stl` in MeshLab or Blender to check for remaining openings.

### `version 'GLIBC_2.38' not found` or `version 'GLIBC_2.36' not found`

You are running a binary that was **not** compiled with the `glibc_compat.c` shim (e.g. a build produced before this fix was applied). The pre-built `obj2msh` binary in this release includes the shim and should not show this error. If you built from source and see this:

1. Ensure `src/glibc_compat.c` is present in your local clone
2. Ensure `CMakeLists.txt` lists it in `OBJ2MSH_SOURCES`
3. Delete your `build/` directory and rebuild from scratch:
   ```bash
   rm -rf build && mkdir build && cd build
   cmake .. -DCMAKE_BUILD_TYPE=Release
   make -j$(nproc)
   ```

You can verify the fixed binary's maximum glibc requirement with:
```bash
objdump -T obj2msh | grep -oP 'GLIBC_[\d.]+' | sort -V | tail -1
# Should print: GLIBC_2.35
```

### File browser shows garbled characters (`@◆~T~` or similar)

This was a bug in earlier releases that has been fixed. The separator line was drawn using the Unicode box-drawing character `─` (U+2500), which renders as multi-byte garbage on terminals whose locale is `C`, `POSIX`, or anything non-UTF-8.

**If you are using the current release binary**, this should not occur — the separator is now drawn with plain ASCII dashes via ncurses `hline()`, and `setlocale(LC_ALL, "")` is called at startup so ncurses correctly reads the terminal's locale.

**If you built from an older source tree**, update `src/file_picker.cpp` from the repository and rebuild.

If you want to confirm your terminal locale:
```bash
locale
# Look for LANG= or LC_ALL= — if it shows "C" or "POSIX", the terminal
# is not configured for UTF-8. The fixed binary handles this correctly.
```

### Terminal garbled after crash

If the tool crashes while ncurses is active, your terminal may look broken. Run:
```bash
reset
```

### Mesh is coarser / finer than expected

Adjust `smallest_feature_to_preserve_in_mm`:
- Too coarse → decrease the value (e.g. from `2.0` to `1.0`)
- Too fine (very slow) → increase the value (e.g. from `2.0` to `4.0`)

### CMake cannot find Eigen3

```bash
sudo apt install libeigen3-dev
# then re-run cmake
```

If Eigen is in a non-standard location:
```bash
cmake .. -DCMAKE_BUILD_TYPE=Release -DEigen3_DIR=/path/to/eigen3/cmake
```

---

## Source File Reference

| File | Responsibility |
|---|---|
| `src/main.cpp` | Entry point. Calls file picker, collects parameters, runs all pipeline stages in sequence. |
| `src/file_picker.cpp` | ncurses interactive file browser (POSIX). Shows only directories and `.obj` files. Arrow keys to navigate. Calls `setlocale(LC_ALL, "")` before `initscr()` so the browser works correctly on terminals without a UTF-8 locale. Separator drawn with `hline()` (plain ASCII dashes) to avoid garbled output from Unicode box-drawing characters. |
| `src/mesh_scale.cpp` | Reads OBJ vertex lines, multiplies coordinates by the scale factor, writes a new OBJ. Prints before/after bounding-box extents. |
| `src/mesh_repair.cpp` | Hole closing (centroid fan), Laplacian smoothing, unreferenced vertex removal, STL export. Uses Eigen for geometry. |
| `src/epsr_calc.cpp` | Reads the STL bounding box (ASCII and binary), computes `epsr = feature / diagonal`. |
| `src/tetwild_runner.cpp` | Locates `FloatTetwild_bin` via a 6-step search (env var, CMake path, same dir as executable via `/proc/self/exe`, CWD, system dirs, PATH). All found paths resolved to absolute form via `fs::absolute()`. Before executing, runs `ldd` on the binary and checks for missing shared libraries — if any are absent (e.g. `libtbb.so.12`), prints the exact `apt install` / `dnf install` / `pacman -S` command and exits cleanly. Builds command string, launches as subprocess, handles output file renaming. |
| `src/msh_convert.cpp` | Parses Gmsh MSH v4.1 or v2.2, extracts nodes and tetrahedra, writes ASCII MSH v2.2 with physical tags. |
| `src/msh_verify.cpp` | Opens the final MSH, reads the header version, counts nodes and tets, prints a pass/fail report. |
| `install_deps.sh` | Bash script that installs all runtime dependencies for `obj2msh` and `FloatTetwild_bin` on Ubuntu/Debian systems. Enables the `universe` apt repository if needed (for `libtbb12`), installs all required packages, and verifies `FloatTetwild_bin` can load all its shared libraries. Include this in every release archive. |
| `src/glibc_compat.c` | **glibc 2.35 compatibility shim.** Provides `__isoc23_strtol`, `__isoc23_strtoul`, and `arc4random` so the binary compiled on GCC 13 / glibc 2.38 runs correctly on Ubuntu 22.04 Jammy (glibc 2.35). Must be included in the build and in the repository. See [glibc Compatibility Shim](#glibc-compatibility-shim--glibc_compatc). |

---

## Dependency Reference

| Dependency | Used by | Why |
|---|---|---|
| Eigen3 | `mesh_repair.cpp`, `epsr_calc.cpp` | Matrix math for Laplacian smoothing and bounding box |
| ncurses | `file_picker.cpp` | Terminal UI for the file browser |
| FloatTetwild | `tetwild_runner.cpp` | Volumetric tetrahedral meshing |
| GMP / MPFR | FloatTetwild (indirect) | Arbitrary-precision arithmetic inside fTetWild |
| geogram | FloatTetwild (indirect) | Geometry primitives inside fTetWild |
| libigl | FloatTetwild (indirect) | Mesh I/O inside fTetWild |
| `glibc_compat.c` (bundled) | Link-time shim | Provides `__isoc23_strtol`, `__isoc23_strtoul`, `arc4random` to keep the binary compatible with glibc 2.35. Original code, no external dependency. |

---

## Tested Models

The following freely available anatomical models have been tested with obj2msh and confirmed to produce valid SOFA-ready volumetric meshes:

| Model | Source | Notes |
|---|---|---|
| Colon | [Z-Anatomy](https://www.z-anatomy.com/) | Open-access anatomical atlas; export the colon surface mesh as OBJ |
| Femur | [Z-Anatomy](https://www.z-anatomy.com/) | Same atlas; export the femur |
| L5 Lumbar Vertebra | [CGTrader — Human Lumbar Vertebrae 3D model](https://www.cgtrader.com/items/255811/download-page) | 3D rendering of the fifth lumbar vertebra (L5) from CT images. Patient: female, age 30. |
| Mandible | [NIH 3D Print Exchange — 3DPX-016800](https://3d.nih.gov/entries/3DPX-016800) | Mandible model from the NIH open-access biomedical 3D library |

**Recommended starting parameters for these models:**

| Model | OBJ unit | Scale factor | Feature size | Rationale |
|---|---|---|---|---|
| Colon | metres | `1000.0` | `2.0` | Thin-walled tubular structure; 2 mm preserves wall thickness |
| Femur | metres | `1000.0` | `2.0` | Standard long bone; 2 mm captures cortical detail well |
| L5 vertebra | metres | `1.0` | `1.0` | Trabecular geometry; reduce to `0.5` for fine pore detail |
| Mandible | metres | `1.0` | `0.25` | Fine dental/cortical geometry requires sub-millimetre resolution |

> These sources provide meshes for research and educational use. Check each source's individual licence before redistribution.

---

## Licence Notes

- `obj2msh` pipeline code: translated from the original Python script, same terms apply
- `src/glibc_compat.c`: original code written for this project; no glibc source was used or reproduced; you own it and may licence it under any terms
- Eigen3: MPL 2.0 — https://eigen.tuxfamily.org
- ncurses: MIT-style — https://invisible-island.net/ncurses/

### FloatTetwild redistribution

FloatTetwild is licensed under the **Mozilla Public License 2.0 (MPL 2.0)**. You are permitted to distribute its compiled binary alongside `obj2msh`. MPL 2.0 is not GPL — it does not impose any licence requirements on your own code. The obligations that apply when you redistribute the FloatTetwild binary are:

| Obligation | What you must do | How to satisfy it |
|---|---|---|
| Source availability | The FloatTetwild source must be available | Link to `https://github.com/wildmeshing/fTetWild` — you do not need to host it yourself |
| Attribution | Credit the original authors | Include a `THIRD_PARTY_LICENSES.md` file (template below) |
| Licence text | Include the MPL 2.0 licence | Link to it in `THIRD_PARTY_LICENSES.md` or include the full text |
| No misrepresentation | Do not claim you wrote FloatTetwild | Covered by the attribution above |

Create a file called `THIRD_PARTY_LICENSES.md` in your repository root and in every release archive you publish:

```markdown
# Third-Party Licences

## FloatTetwild

This software uses and distributes a compiled binary of **FloatTetwild**,
a robust tetrahedral meshing library.

- **Authors:** Yixin Hu, Teseo Schneider, Boris Kamer, Denis Zorin, Daniele Panozzo
- **Source code:** https://github.com/wildmeshing/fTetWild
- **Licence:** Mozilla Public License 2.0 (MPL 2.0)
  https://www.mozilla.org/en-US/MPL/2.0/

The FloatTetwild source code has not been modified. The binary is
distributed here as a convenience for users of obj2msh. Per MPL 2.0,
the source remains available at the link above.
```

That is all that is required. You do not need to open-source `obj2msh` itself, change its licence, or make any other modifications to comply with MPL 2.0.

---

*For Windows users, see `README_WINDOWS.md`.*
