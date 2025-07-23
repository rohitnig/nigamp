@echo off
echo Testing Callback Architecture
echo =============================

echo.
echo 1. Building the project...
cmake -B build -G "MinGW Makefiles" -DCMAKE_CXX_COMPILER=C:/mingw64/bin/g++.exe
if %ERRORLEVEL% neq 0 (
    echo CMake configuration failed!
    exit /b 1
)

cmake --build build
if %ERRORLEVEL% neq 0 (
    echo Build failed!
    exit /b 1
)

echo.
echo 2. Running callback architecture unit tests...
echo    (These test the callback mechanism without real audio files)
build\tests\nigamp_tests.exe --gtest_filter="CallbackArchitecture.*"

echo.
echo 3. Running a quick integration test with real audio...
echo    (This will play a short test file to verify callbacks work end-to-end)
if exist "tests\data\test1.mp3" (
    echo Playing test1.mp3 for 5 seconds to verify callback timing...
    timeout /t 1 /nobreak > nul
    build\nigamp.exe --file "tests\data\test1.mp3" --preview
) else (
    echo No test MP3 files found. Skipping integration test.
    echo Run create_test_mp3s.bat first to generate test files.
)

echo.
echo 4. Testing timeout fallback mechanism...
echo    (This would require a mock that never fires callback - see unit tests)

echo.
echo Testing complete!
echo.
echo To manually test:
echo   1. Run: build\nigamp_tests.exe --gtest_filter="CallbackArchitecture.*"
echo   2. Run: build\nigamp.exe --file "tests\data\test1.mp3" --preview
echo   3. Watch console output for "Audio playback completed successfully" messages