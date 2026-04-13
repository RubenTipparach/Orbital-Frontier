@echo off
setlocal

echo === Orbital Frontier: Setup ===
echo.
echo Prerequisites:
echo   - CMake 3.16+  (https://cmake.org/download/)
echo   - One of:
echo       Visual Studio 2022 (MSVC)
echo       MinGW-w64 (GCC)
echo.

:: Check CMake
where cmake >nul 2>&1
if errorlevel 1 (
    echo [ERROR] CMake not found. Install from https://cmake.org/download/
    exit /b 1
)
echo [OK] CMake found:
cmake --version | findstr /R "cmake version"

:: Check for Visual Studio
where cl >nul 2>&1
if not errorlevel 1 (
    echo [OK] MSVC compiler found
    echo.
    echo Configuring with Visual Studio...
    cmake -B build -G "Visual Studio 17 2022"
    goto :done
)

:: Check for MinGW
where gcc >nul 2>&1
if not errorlevel 1 (
    echo [OK] GCC (MinGW) found
    echo.
    echo Configuring with MinGW Makefiles...
    cmake -B build -G "MinGW Makefiles"
    goto :done
)

echo [ERROR] No C compiler found. Install Visual Studio 2022 or MinGW-w64.
exit /b 1

:done
echo.
echo Setup complete. Run build-run.bat to build and launch.
