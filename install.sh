#!/usr/bin/env bash
# Hummingbird One-Liner Installer
# Usage: curl -sSL https://raw.githubusercontent.com/prayangshuuu/Hummingbird/main/install.sh | bash

set -e

echo "========================================="
echo "        Hummingbird Installer"
echo "========================================="

# 1. Check requirements
command -v git >/dev/null 2>&1 || { echo >&2 "Error: git is required but not installed. Aborting."; exit 1; }
command -v cmake >/dev/null 2>&1 || { echo >&2 "Error: cmake is required but not installed. Aborting."; exit 1; }

# 2. Clone the repository if not already inside it
if [ ! -d ".git" ] || [ ! -f "CMakeLists.txt" ]; then
    echo "[1/4] Cloning Hummingbird repository..."
    if [ -d "hummingbird" ]; then
        echo "Directory 'hummingbird' already exists. Entering..."
        cd hummingbird
    else
        git clone https://github.com/prayangshuuu/Hummingbird.git hummingbird
        cd hummingbird
    fi
else
    echo "[1/4] Running inside existing Hummingbird repository..."
fi

# 3. Configure CMake
echo "[2/4] Configuring build..."
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DHB_BUILD_FRONTENDS=ON

# 4. Build
echo "[3/4] Building Hummingbird (Release mode)..."
cmake --build build --config Release -j 4

# 5. Install (Local user bin)
echo "[4/4] Installing binaries..."
INSTALL_DIR="$HOME/.local/bin"
mkdir -p "$INSTALL_DIR"

if [ -f "build/frontends/cli/hb" ]; then
    cp build/frontends/cli/hb "$INSTALL_DIR/hb"
    echo "Installed 'hb' to $INSTALL_DIR/hb"
elif [ -f "build/frontends/cli/Release/hb.exe" ]; then
    # Windows/MSVC fallback if running in bash on windows
    cp build/frontends/cli/Release/hb.exe "$INSTALL_DIR/hb.exe"
    echo "Installed 'hb.exe' to $INSTALL_DIR/hb.exe"
elif [ -f "build/frontends/cli/hb.exe" ]; then
    # MinGW fallback
    cp build/frontends/cli/hb.exe "$INSTALL_DIR/hb.exe"
    echo "Installed 'hb.exe' to $INSTALL_DIR/hb.exe"
else
    echo "Warning: Could not find built 'hb' binary."
fi

# Try to add to PATH if not already
if [[ ":$PATH:" != *":$INSTALL_DIR:"* ]]; then
    echo "-----------------------------------------"
    echo "Notice: $INSTALL_DIR is not in your PATH."
    echo "Please add it by running:"
    echo "  export PATH=\"\$HOME/.local/bin:\$PATH\""
    echo "Or add it to your ~/.bashrc or ~/.zshrc."
    echo "-----------------------------------------"
fi

echo "========================================="
echo " Hummingbird successfully installed!"
echo " Test it with: hb --version"
echo "========================================="
