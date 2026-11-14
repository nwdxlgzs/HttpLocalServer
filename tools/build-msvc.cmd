@echo off
setlocal
chcp 65001 >nul
set VSLANG=1033
if not defined VSCMD_VER call "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b 1
set VSLANG=1033
if not defined HLS_ENABLE_TLS set HLS_ENABLE_TLS=OFF
cmake -S "%~dp0.." -B "%~dp0..\build-msvc" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl -DHLS_STATIC_RUNTIME=ON -DHLS_ENABLE_TLS=%HLS_ENABLE_TLS%
if errorlevel 1 exit /b 1
rem Clean build prevents localized Ninja include-prefix failures from leaving stale ABI objects.
cmake --build "%~dp0..\build-msvc" --clean-first --parallel 4
if errorlevel 1 exit /b 1
ctest --test-dir "%~dp0..\build-msvc" --output-on-failure
