#!/bin/bash
# Build Test Script for 6-Channel Software Looper
# This script tests if the project compiles successfully

set -e  # Exit on error

echo "================================"
echo "6-Channel Looper Build Test"
echo "================================"
echo ""

# Check for CMake
if ! command -v cmake &> /dev/null; then
    echo "❌ ERROR: CMake not found"
    echo "Please install CMake 3.15 or higher"
    exit 1
fi

echo "✓ CMake found: $(cmake --version | head -1)"

# Check for JUCE
echo ""
echo "Checking for JUCE..."
if [ -d "$HOME/JUCE" ]; then
    echo "✓ JUCE found in $HOME/JUCE"
elif [ -d "/usr/local/lib/cmake/JUCE" ]; then
    echo "✓ JUCE found in /usr/local/lib/cmake/JUCE"
else
    echo "⚠ WARNING: JUCE not found in standard locations"
    echo "Make sure JUCE is installed and CMake can find it"
fi

# Create build directory
echo ""
echo "Creating build directory..."
if [ -d "build" ]; then
    echo "⚠ build/ directory exists, removing..."
    rm -rf build
fi
mkdir build
cd build

# Configure
echo ""
echo "================================"
echo "Running CMake Configuration..."
echo "================================"
cmake .. -DCMAKE_BUILD_TYPE=Debug

# Build
echo ""
echo "================================"
echo "Building Project..."
echo "================================"
cmake --build . -- -j$(nproc 2>/dev/null || echo 4)

# Check if binaries were created
echo ""
echo "================================"
echo "Verifying Build..."
echo "================================"

if [ -f "LooperGUI" ] || [ -f "LooperGUI.exe" ] || [ -f "Debug/LooperGUI.exe" ]; then
    echo "✓ LooperGUI built successfully"
else
    echo "❌ LooperGUI not found"
    exit 1
fi

if [ -f "LooperTest" ] || [ -f "LooperTest.exe" ] || [ -f "Debug/LooperTest.exe" ]; then
    echo "✓ LooperTest built successfully"
else
    echo "❌ LooperTest not found"
    exit 1
fi

echo ""
echo "================================"
echo "✅ BUILD SUCCESSFUL!"
echo "================================"
echo ""
echo "You can now run:"
echo "  ./LooperGUI     - GUI Application"
echo "  ./LooperTest    - Console Test"
echo ""
