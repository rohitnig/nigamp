@echo off
echo Building hotkey test...

if not exist "build_test" (
    mkdir build_test
)

cd build_test

cmake -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Debug ..\tests
if %errorlevel% neq 0 (
    echo CMake configuration failed!
    pause
    exit /b 1
)

mingw32-make test_hotkey_handler
if %errorlevel% neq 0 (
    echo Build failed!
    pause
    exit /b 1
)

echo.
echo Running hotkey test...
echo.
test_hotkey_handler.exe

cd ..
pause