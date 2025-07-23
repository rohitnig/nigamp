@echo off
setlocal enabledelayedexpansion

echo Creating 10-second test MP3s from C:\Music\

set count=0
for %%f in ("C:\Music\*.mp3") do (
    if !count! lss 5 (
        set /a count+=1
        echo Creating test!count!.mp3 from "%%~nxf"
        ffmpeg -i "%%f" -t 10 -c copy "test!count!.mp3" -y
    )
)

echo Done! Created %count% test MP3 files.
pause