#!/bin/bash
echo "Setting up Nigamp Dependencies"
echo "==============================="

# Create third_party directory if it doesn't exist
if [ ! -d "third_party" ]; then
    echo "Creating third_party directory..."
    mkdir -p third_party
fi

echo ""
echo "Checking for required libraries..."
echo ""

# Check for ALSA development libraries
if ! pkg-config --exists alsa; then
    echo "ALSA development libraries not found."
    echo "Please install with: sudo apt-get install libasound2-dev"
    echo ""
else
    echo "✓ ALSA libraries found"
fi

# Check for CMake
if ! command -v cmake &> /dev/null; then
    echo "CMake not found."
    echo "Please install with: sudo apt-get install cmake"
    echo ""
else
    echo "✓ CMake found"
fi

# Check for C++ compiler
if ! command -v g++ &> /dev/null; then
    echo "G++ compiler not found."
    echo "Please install with: sudo apt-get install build-essential"
    echo ""
else
    echo "✓ G++ compiler found"
fi

echo ""
echo "Downloading header-only libraries..."
echo ""

# Download dr_wav.h if curl or wget is available
if command -v curl &> /dev/null; then
    echo "Downloading dr_wav.h..."
    curl -L -o third_party/dr_wav.h https://raw.githubusercontent.com/mackron/dr_libs/master/dr_wav.h
    if [ $? -eq 0 ]; then
        echo "✓ dr_wav.h downloaded successfully!"
    else
        echo "Failed to download dr_wav.h automatically"
    fi
elif command -v wget &> /dev/null; then
    echo "Downloading dr_wav.h..."
    wget -O third_party/dr_wav.h https://raw.githubusercontent.com/mackron/dr_libs/master/dr_wav.h
    if [ $? -eq 0 ]; then
        echo "✓ dr_wav.h downloaded successfully!"
    else
        echo "Failed to download dr_wav.h automatically"
    fi
else
    echo "curl or wget not found - please download dr_wav.h manually:"
    echo "  URL: https://raw.githubusercontent.com/mackron/dr_libs/master/dr_wav.h"
    echo "  Destination: third_party/dr_wav.h"
fi

# Check if minimp3.h exists
if [ ! -f "third_party/minimp3.h" ]; then
    echo ""
    echo "minimp3.h not found in third_party directory."
    echo "Please ensure minimp3.h is present in third_party/"
fi

echo ""
echo "Setup complete!"
echo "After installing any missing dependencies, run ./build.sh to compile the project."
echo ""

