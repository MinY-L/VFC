@echo off
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d "%~dp0"

set BUILD=build\Desktop_Qt_6_8_2_MSVC2022_64bit-Release
set QTBIN=C:\Qt\6.8.2\msvc2022_64\bin
set VER=0.1.0
set STAGE=release\VFC-%VER%-windows-x64
set ZIP=release\VFC-%VER%-windows-x64.zip

echo [1/4] Build Release
cmake --build %BUILD% --config Release
if errorlevel 1 exit /b 1

echo [2/4] Stage files
if exist release rmdir /s /q release
mkdir %STAGE%
copy /y %BUILD%\VFC.exe %STAGE%\VFC.exe >nul

echo [3/4] windeployqt
%QTBIN%\windeployqt.exe --release --compiler-runtime --no-translations %STAGE%\VFC.exe
if errorlevel 1 exit /b 1

echo FFmpeg is required. Install with: winget install Gyan.FFmpeg> %STAGE%\README.txt
echo.>> %STAGE%\README.txt
echo 运行前请安装 FFmpeg，并确保 ffmpeg 与 ffprobe 在 PATH 中。>> %STAGE%\README.txt
echo 推荐命令：winget install Gyan.FFmpeg>> %STAGE%\README.txt

echo [4/4] Zip
powershell.exe -NoProfile -Command "Compress-Archive -Path '%CD%\%STAGE%\*' -DestinationPath '%CD%\%ZIP%' -Force"
if errorlevel 1 exit /b 1

echo.
echo OK: %CD%\%ZIP%
exit /b 0
