@echo off
setlocal

echo === Orbital Frontier: Windows Build ===

:: Configure if needed
if not exist build\CMakeCache.txt (
    echo [1/3] Configuring CMake...
    cmake -B build -G "Visual Studio 17 2022"
    if errorlevel 1 goto :error
) else (
    echo [1/3] CMake already configured
)

:: Build
echo [2/3] Building (Release)...
cmake --build build --config Release
if errorlevel 1 goto :error

:: Run
echo [3/3] Launching orbital-frontier.exe
if exist build\Release\orbital-frontier.exe (
    start "" build\Release\orbital-frontier.exe
) else if exist build\orbital-frontier.exe (
    start "" build\orbital-frontier.exe
) else (
    echo ERROR: orbital-frontier.exe not found in build/
    goto :error
)
goto :done

:error
echo.
echo BUILD FAILED
exit /b 1

:done
echo Done.
