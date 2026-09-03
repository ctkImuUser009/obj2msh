#!/usr/bin/env bash
# =============================================================================
# install_deps.sh
#
# Install all runtime dependencies required by obj2msh and FloatTetwild_bin
# on Debian/Ubuntu-based systems.
#
# Run this once before using obj2msh:
#   chmod +x install_deps.sh
#   ./install_deps.sh
#
# What this installs:
#   obj2msh itself needs:
#     libncurses6  libtinfo6     (ncurses terminal UI)
#
#   FloatTetwild_bin needs:
#     libtbb12     (Intel TBB threading library — in the "universe" repo)
#     libgmp10     (GNU Multiple Precision arithmetic)
#     libmpfr6     (GNU MPFR floating-point library)
#
#   libgcc-s1 and libstdc++6 are also required by FloatTetwild_bin but are
#   virtually always present on any Ubuntu system with build-essential or gcc.
#   They are included here for completeness on truly minimal installs.
#
# Tested on: Ubuntu 22.04 LTS (Jammy Jellyfish)
# =============================================================================

set -e

# ── Colour output ─────────────────────────────────────────────────────────────
RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'; NC='\033[0m'
info()    { echo -e "${GREEN}[INFO]${NC} $*"; }
warn()    { echo -e "${YELLOW}[WARN]${NC} $*"; }
error()   { echo -e "${RED}[ERROR]${NC} $*"; }

echo "============================================================"
echo "  obj2msh dependency installer"
echo "  For Ubuntu 22.04 LTS (Jammy Jellyfish) and compatible"
echo "============================================================"
echo ""

# ── Check we are on a Debian/Ubuntu system ────────────────────────────────────
if ! command -v apt-get &>/dev/null; then
    error "apt-get not found. This script is for Debian/Ubuntu only."
    echo ""
    echo "For Fedora / RHEL / Rocky Linux, run:"
    echo "  sudo dnf install tbb gmp mpfr ncurses"
    echo ""
    echo "For Arch Linux, run:"
    echo "  sudo pacman -S onetbb gmp mpfr ncurses"
    exit 1
fi

# ── Check for sudo ────────────────────────────────────────────────────────────
if [ "$EUID" -ne 0 ]; then
    if ! command -v sudo &>/dev/null; then
        error "This script must be run as root, or sudo must be available."
        exit 1
    fi
    SUDO="sudo"
else
    SUDO=""
fi

# ── Enable the universe repository (required for libtbb12) ───────────────────
info "Checking apt repositories..."
if ! apt-cache show libtbb12 &>/dev/null; then
    warn "libtbb12 not found in current apt sources. Enabling universe repository..."
    if command -v add-apt-repository &>/dev/null; then
        $SUDO add-apt-repository -y universe
    else
        # Fallback: edit sources.list directly
        if grep -q "^deb.*jammy.*main$" /etc/apt/sources.list; then
            $SUDO sed -i 's/^\(deb.*jammy.*main\)$/\1 universe/' /etc/apt/sources.list
        fi
    fi
    info "Updating apt package list..."
    $SUDO apt-get update -qq
else
    info "Universe repository already enabled."
fi

# ── Package list ──────────────────────────────────────────────────────────────
PACKAGES=(
    # obj2msh runtime deps
    libncurses6       # ncurses terminal library (file picker UI)
    libtinfo6         # terminal info library (ncurses dependency)

    # FloatTetwild_bin runtime deps
    libtbb12          # Intel TBB 2021 threading library (libtbb.so.12)
    libgmp10          # GNU Multiple Precision arithmetic (libgmp.so.10)
    libmpfr6          # GNU MPFR floating-point (libmpfr.so.6)
    libgomp1          # OpenMP runtime (libgomp.so.1)
    libgcc-s1         # GCC runtime (libgcc_s.so.1)
    libstdc++6        # C++ standard library (libstdc++.so.6)
)

# ── Install ───────────────────────────────────────────────────────────────────
info "Installing packages: ${PACKAGES[*]}"
echo ""
$SUDO apt-get install -y "${PACKAGES[@]}"

echo ""
echo "============================================================"
info "Installation complete."
echo ""

# ── Verify FloatTetwild_bin if it is present beside this script ───────────────
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
FTET_BIN="$SCRIPT_DIR/FloatTetwild_bin"

if [ -f "$FTET_BIN" ]; then
    info "Checking FloatTetwild_bin shared library dependencies..."
    MISSING=$(ldd "$FTET_BIN" 2>/dev/null | grep "not found" || true)
    if [ -z "$MISSING" ]; then
        info "FloatTetwild_bin: all libraries satisfied."
    else
        warn "FloatTetwild_bin still has unresolved libraries:"
        echo "$MISSING"
        warn "You may need to install additional packages."
        warn "Run:  ldd \"$FTET_BIN\"  to see details."
    fi
else
    warn "FloatTetwild_bin not found beside this script — skipping library check."
    warn "Place FloatTetwild_bin in: $SCRIPT_DIR"
fi

echo ""
info "You can now run obj2msh:"
echo "    cd \"$SCRIPT_DIR\""
echo "    ./obj2msh"
echo "============================================================"
