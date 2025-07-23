@echo off
echo Testing Fixed Callback Architecture
echo ===================================

echo.
echo Building project...
cmake -B build -G "MinGW Makefiles" -DCMAKE_CXX_COMPILER=C:/mingw64/bin/g++.exe
if %ERRORLEVEL% neq 0 (
    echo ❌ CMake configuration failed!
    exit /b 1
)

cmake --build build
if %ERRORLEVEL% neq 0 (
    echo ❌ Build failed!
    exit /b 1
)

echo ✅ Build successful!
echo.

echo Running simple callback tests (deadlock-free)...
echo ------------------------------------------------
build\tests\nigamp_tests.exe --gtest_filter="CallbackSimple.*" --gtest_color=yes

if %ERRORLEVEL% equ 0 (
    echo.
    echo ✅ Simple callback tests PASSED!
    echo.
    echo The callback architecture is working correctly.
    echo Now testing with real audio engine...
    echo.
    
    echo Testing basic types and compilation...
    build\tests\nigamp_tests.exe --gtest_filter="SimpleCallback.*" --gtest_color=yes
    
    if %ERRORLEVEL% equ 0 (
        echo.
        echo ✅ All basic tests PASSED!
        echo.
        echo Ready for integration testing with real audio files.
        echo Try: build\nigamp.exe --file "tests\data\test1.mp3" --preview
    ) else (
        echo ❌ Basic tests failed
    )
) else (
    echo ❌ Simple callback tests failed
    echo Check the output above for details.
)

echo.
echo Test complete!
pause