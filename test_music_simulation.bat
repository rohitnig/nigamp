@echo off
echo Building music player simulation test...

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

mingw32-make test_music_player_simulation
if %errorlevel% neq 0 (
    echo Build failed!
    pause
    exit /b 1
)

echo.
echo Running music player simulation test...
echo.
test_music_player_simulation.exe

cd ..
pause