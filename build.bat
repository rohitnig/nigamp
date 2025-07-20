@echo off
echo Building Nigamp - Ultra-Lightweight MP3 Player
echo ==============================================

REM Check if build directory exists
if not exist build (
    echo Creating build directory...
    mkdir build
)

REM Set PATH to include MinGW
set PATH=C:\mingw64\bin;%PATH%

REM Configure with CMake
echo Configuring with CMake...
cmake -B build -G "MinGW Makefiles" -DCMAKE_CXX_COMPILER=C:/mingw64/bin/g++.exe -DCMAKE_C_COMPILER=C:/mingw64/bin/gcc.exe
if errorlevel 1 (
    echo Error: CMake configuration failed
    exit /b 1
)

REM Build the project
echo Building project...
cmake --build build
if errorlevel 1 (
    echo Error: Build failed
    exit /b 1
)

echo Build completed successfully!
echo Executable: build\nigamp.exe

REM Ask if user wants to run tests
set /p run_tests="Run tests? (y/n): "
if /i "%run_tests%"=="y" (
    echo Running tests...
    ctest --test-dir build --verbose
)

echo Done!