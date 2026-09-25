@echo off
REM ===========================================================================
REM build_all_tests.bat - build and run the full host-side regression suite.
REM
REM   test_vfs.exe  : legacy VFS regression (119 checks, do not break)
REM   test_net.exe  : IPv4 checksum/header, TCP state machine, DNS
REM   test_fs.exe   : GPT, FAT32 helpers, EXT2 superblock, NTFS MFT walk
REM   test_gui.exe  : osui component registry + window z-order/clip/hit
REM
REM Each test exe returns 0 iff all its checks passed. The script tallies the
REM results and exits non-zero if any suite failed.
REM
REM Requirements: MinGW gcc on PATH, no kernel / QEMU environment needed.
REM ===========================================================================
setlocal enabledelayedexpansion
set ROOT=%~dp0..
set DIR=%~dp0
set FAILS=0

echo ============================================================
echo Building and running host regression suites
echo ============================================================

REM ---- legacy VFS suite (unchanged build line) --------------------------
echo.
echo [1/4] test_vfs
gcc -std=gnu17 -fno-builtin -Wall -Wno-unused-parameter -Wno-unused-function -g ^
    -D__HOST_TEST__ ^
    -I "%ROOT%\include" -I "%DIR%." ^
    -o "%DIR%test_vfs.exe" "%DIR%test_vfs.c" "%DIR%stubs.c"
if errorlevel 1 (echo   BUILD FAILED & set /a FAILS+=1 & goto :net)
"%DIR%test_vfs.exe"
if errorlevel 1 set /a FAILS+=1

:net
echo.
echo [2/4] test_net
gcc -std=gnu17 -fno-builtin -Wall -Wno-unused-parameter -Wno-unused-function -g ^
    -I "%ROOT%\include" -I "%ROOT%\kernel\net" -I "%DIR%." ^
    -o "%DIR%test_net.exe" "%DIR%test_net.c" "%DIR%stubs_ext.c"
if errorlevel 1 (echo   BUILD FAILED & set /a FAILS+=1 & goto :fs)
"%DIR%test_net.exe"
if errorlevel 1 set /a FAILS+=1

:fs
echo.
echo [3/4] test_fs
gcc -std=gnu17 -fno-builtin -Wall -Wno-unused-parameter -Wno-unused-function -g ^
    -I "%ROOT%\include" -I "%DIR%." ^
    -o "%DIR%test_fs.exe" "%DIR%test_fs.c" "%DIR%stubs_ext.c"
if errorlevel 1 (echo   BUILD FAILED & set /a FAILS+=1 & goto :gui)
"%DIR%test_fs.exe"
if errorlevel 1 set /a FAILS+=1

:gui
echo.
echo [4/4] test_gui
gcc -std=gnu17 -fno-builtin -Wall -Wno-unused-parameter -Wno-unused-function -g ^
    -I "%ROOT%\include" -I "%DIR%." ^
    -o "%DIR%test_gui.exe" "%DIR%test_gui.c" "%DIR%stubs_ext.c"
if errorlevel 1 (echo   BUILD FAILED & set /a FAILS+=1 & goto :summary)
"%DIR%test_gui.exe"
if errorlevel 1 set /a FAILS+=1

:summary
echo.
echo ============================================================
if %FAILS%==0 (
    echo ALL HOST SUITES PASSED
    exit /b 0
) else (
    echo %FAILS% SUITE^(S^) FAILED
    exit /b 1
)
