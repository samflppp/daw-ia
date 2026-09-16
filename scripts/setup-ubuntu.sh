#!/usr/bin/env bash
# Installs the full native toolchain for the core/ module on Ubuntu 24.04 LTS.
# Also works on 22.04 (falls back to the distro clang version).
#
# Usage:  ./scripts/setup-ubuntu.sh            # toolchain + JUCE deps + Python tooling
#         ./scripts/setup-ubuntu.sh --ci       # no Python tooling, no alternatives switch
set -euo pipefail

CI_MODE=0
[[ "${1:-}" == "--ci" ]] && CI_MODE=1

if [[ "$(id -u)" -eq 0 ]]; then SUDO=""; else SUDO="sudo"; fi

. /etc/os-release
echo "==> Ubuntu ${VERSION_ID} (${VERSION_CODENAME})"

export DEBIAN_FRONTEND=noninteractive
$SUDO apt-get update -y

# --- Toolchain ---------------------------------------------------------------
# Clang is the reference compiler on Linux. GCC stays installed because
# libstdc++ comes with it and some tools expect it.
TOOLCHAIN=(
    build-essential
    pkg-config
    git
    git-lfs
    ninja-build
    ccache
    clang
    clang-format
    clang-tidy
    lld
    lldb
    gdb
    python3
    python3-venv
    python3-pip
    curl
    ca-certificates
)

# --- JUCE system dependencies (docs/Linux Dependencies.md, JUCE 8) -----------
JUCE_DEPS=(
    libasound2-dev
    libjack-jackd2-dev
    ladspa-sdk
    libcurl4-openssl-dev
    libfreetype-dev
    libfontconfig1-dev
    libx11-dev
    libxcomposite-dev
    libxcursor-dev
    libxext-dev
    libxinerama-dev
    libxrandr-dev
    libxrender-dev
    libglu1-mesa-dev
    mesa-common-dev
)

# WebKit is only needed if JUCE_WEB_BROWSER=1. The project builds with
# JUCE_WEB_BROWSER=0 (no webview), but the headers are cheap to have.
if apt-cache show libwebkit2gtk-4.1-dev >/dev/null 2>&1; then
    JUCE_DEPS+=(libwebkit2gtk-4.1-dev)
else
    JUCE_DEPS+=(libwebkit2gtk-4.0-dev)
fi

# Headless display for running GUI tests in CI or over SSH.
EXTRA=(xvfb)

$SUDO apt-get install -y --no-install-recommends "${TOOLCHAIN[@]}" "${JUCE_DEPS[@]}" "${EXTRA[@]}"

# --- CMake -------------------------------------------------------------------
# JUCE needs CMake >= 3.22. Ubuntu 24.04 ships 3.28, 22.04 ships 3.22.
# Kitware's APT repository gives a recent version on both.
CMAKE_MIN="3.25"
current_cmake="$(cmake --version 2>/dev/null | head -n1 | awk '{print $3}' || true)"
if [[ -z "${current_cmake}" ]] || [[ "$(printf '%s\n' "${CMAKE_MIN}" "${current_cmake}" | sort -V | head -n1)" != "${CMAKE_MIN}" ]]; then
    echo "==> Installing CMake from Kitware APT repository"
    $SUDO apt-get install -y --no-install-recommends gpg wget
    wget -qO- https://apt.kitware.com/keys/kitware-archive-latest.asc \
        | gpg --dearmor \
        | $SUDO tee /usr/share/keyrings/kitware-archive-keyring.gpg >/dev/null
    echo "deb [signed-by=/usr/share/keyrings/kitware-archive-keyring.gpg] https://apt.kitware.com/ubuntu/ ${VERSION_CODENAME} main" \
        | $SUDO tee /etc/apt/sources.list.d/kitware.list >/dev/null
    $SUDO apt-get update -y
    $SUDO apt-get install -y --no-install-recommends cmake
else
    echo "==> CMake ${current_cmake} already installed"
fi

# --- Make clang the default cc/c++ -------------------------------------------
if [[ "${CI_MODE}" -eq 0 ]]; then
    $SUDO update-alternatives --install /usr/bin/cc  cc  "$(readlink -f "$(command -v clang)")"   100
    $SUDO update-alternatives --install /usr/bin/c++ c++ "$(readlink -f "$(command -v clang++)")" 100
fi

# --- Python tooling for services/ --------------------------------------------
if [[ "${CI_MODE}" -eq 0 ]]; then
    if ! command -v uv >/dev/null 2>&1; then
        echo "==> Installing uv (Python project manager)"
        curl -LsSf https://astral.sh/uv/install.sh | sh
    fi
fi

echo
echo "==> Versions"
cmake --version | head -n1
ninja --version
clang --version | head -n1
clang-format --version
python3 --version
echo
echo "Done. Next: ./scripts/bootstrap-submodules.sh"
