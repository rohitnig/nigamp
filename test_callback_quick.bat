@echo off
echo Quick Callback Architecture Test
echo ================================

echo.
echo Step 1: Testing simple callback compilation and execution...
echo --------------------------------------------------------

echo Compiling quick test...
g++ -I include -I src -std=c++17 quick_callback_test.cpp src\audio_engine.cpp -lwinmm -ldsound -lole32 -o quick_test.exe 2>&1

if %ERRORLEVEL% neq 0 (
    echo ❌ Compilation failed!
    echo.
    echo This suggests there are compilation issues with the callback architecture.
    echo Check the compiler output above for details.
    pause
    exit /b 1
)

echo ✅ Compilation successful!
echo.
echo Running quick test...
echo --------------------
quick_test.exe

if %ERRORLEVEL% neq 0 (
    echo ❌ Quick test failed!
) else (
    echo ✅ Quick test passed!
)

echo.
echo Step 2: Testing simple unit tests...
echo -----------------------------------

echo Building project...
cmake -B build -G "MinGW Makefiles" -DCMAKE_CXX_COMPILER=C:/mingw64/bin/g++.exe > build_log.txt 2>&1
cmake --build build >> build_log.txt 2>&1

if %ERRORLEVEL% neq 0 (
    echo ❌ Build failed! Check build_log.txt for details.
    exit /b 1
)

echo ✅ Build successful!
echo.
echo Running simple callback tests only...
build\tests\nigamp_tests.exe --gtest_filter="SimpleCallback.*"

echo.
echo Step 3: If simple tests pass, try the full callback tests...
echo ----------------------------------------------------------
echo build\tests\nigamp_tests.exe --gtest_filter="CallbackArchitecture.*"

echo.
echo Test complete! 
echo.
echo Next steps:
echo 1. If quick test passed: Basic callback architecture works
echo 2. If unit tests pass: Mock engine works correctly  
echo 3. If both pass: Try real integration test with audio files

pause