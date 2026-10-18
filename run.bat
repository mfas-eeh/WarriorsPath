@echo off
cd /d "%~dp0"
cmake -S . -B build -G "Ninja"
cmake --build build
if %ERRORLEVEL% EQU 0 (
    cd /d "%~dp0build"
    PathExplorer.exe
)
