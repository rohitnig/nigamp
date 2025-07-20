@echo off
echo Copying dr_wav.h to correct location...

if exist "include\dr_wav.h" (
    echo Found dr_wav.h in include directory
    copy "include\dr_wav.h" "third_party\dr_wav.h"
    if errorlevel 0 (
        echo dr_wav.h copied successfully to third_party directory!
        del "include\dr_wav.h"
        echo Removed dr_wav.h from include directory
    ) else (
        echo Error copying file
    )
) else (
    echo dr_wav.h not found in include directory
    if exist "third_party\dr_wav.h" (
        echo dr_wav.h already in correct location
    ) else (
        echo Please place dr_wav.h in the include directory first
    )
)

pause