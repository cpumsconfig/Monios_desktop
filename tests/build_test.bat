@echo off
REM Build and run the host-side VFS regression tests.
setlocal
set ROOT=%~dp0..

echo == Compiling test_vfs.exe ==
gcc -std=gnu17 -fno-builtin -Wall -Wno-unused-parameter -Wno-unused-function -g ^
    -D__HOST_TEST__ ^
    -I "%ROOT%\include" -I "%~dp0." ^
    -o "%~dp0test_vfs.exe" "%~dp0test_vfs.c" "%~dp0stubs.c"
if errorlevel 1 (
    echo BUILD FAILED
    exit /b 1
)

echo == Running ==
"%~dp0test_vfs.exe"
exit /b %errorlevel%
