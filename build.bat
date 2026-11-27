@echo off
setlocal
cd /d "%~dp0"
if not defined CXX set "CXX=g++"
"%CXX%" -std=c++17 -O2 -Wall -Wextra -pedantic -static -Iinclude src/main.cpp -o reconstruct.exe
if errorlevel 1 exit /b 1
echo Built: %CD%\reconstruct.exe
