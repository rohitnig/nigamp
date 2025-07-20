@echo off
echo Setting up Nigamp Dependencies
echo ===============================

REM Create third_party directory if it doesn't exist
if not exist third_party (
    echo Creating third_party directory...
    mkdir third_party
)

echo.
echo Required libraries to download manually:
echo.
echo 1. dr_wav.h
echo    URL: https://raw.githubusercontent.com/mackron/dr_libs/master/dr_wav.h
echo    Destination: third_party\dr_wav.h
echo.
echo 2. libmpg123
echo    URL: https://www.mpg123.de/download.shtml
echo    - Download Windows binaries
echo    - Extract headers to third_party\mpg123\include\
echo    - Extract libraries to third_party\mpg123\lib\
echo.

REM Try to download dr_wav.h if curl is available
where curl >nul 2>nul
if %errorlevel%==0 (
    echo Attempting to download dr_wav.h...
    curl -L -o third_party\dr_wav.h https://raw.githubusercontent.com/mackron/dr_libs/master/dr_wav.h
    if errorlevel 0 (
        echo dr_wav.h downloaded successfully!
    ) else (
        echo Failed to download dr_wav.h automatically
    )
) else (
    echo curl not found - please download dr_wav.h manually
)

echo.
echo Please download libmpg123 manually from the URL above.
echo After downloading all libraries, run build.bat to compile the project.
echo.
pause