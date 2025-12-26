#!/bin/bash
echo "Building Nigamp - Ultra-Lightweight MP3 Player"
echo "=============================================="

# Check if build directory exists
if [ ! -d "build" ]; then
    echo "Creating build directory..."
    mkdir -p build
fi

# Configure with CMake
echo "Configuring with CMake..."
cmake -B build
if [ $? -ne 0 ]; then
    echo "Error: CMake configuration failed"
    exit 1
fi

# Build the project
echo "Building project..."
cmake --build build
if [ $? -ne 0 ]; then
    echo "Error: Build failed"
    exit 1
fi

echo "Build completed successfully!"
echo "Executable: build/nigamp"

# Ask if user wants to run tests
read -p "Run tests? (y/n): " run_tests
if [ "$run_tests" = "y" ] || [ "$run_tests" = "Y" ]; then
    echo "Running tests..."
    ctest --test-dir build --verbose
fi

echo "Done!"

