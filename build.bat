@echo off
chcp 65001 >nul
setlocal enabledelayedexpansion

REM PC Mining Program Build Script
REM For Windows Systems

echo === PC Mining Program Build Script ===
echo.

REM Check system dependencies
echo Checking system dependencies...

REM Check CMake
cmake --version >nul 2>&1
if errorlevel 1 (
    echo Error: CMake not found, please install CMake 3.12+
    echo Download: https://cmake.org/download/
    pause
    exit /b 1
)

REM Check Visual Studio or Build Tools
where cl >nul 2>&1
if errorlevel 1 (
    echo Error: MSVC compiler not found, please install Visual Studio or Build Tools
    echo Download: https://visualstudio.microsoft.com/downloads/
    pause
    exit /b 1
)

echo [OK] All dependencies check passed
echo.

REM Get CPU information
echo Detecting CPU features...
wmic cpu get name /format:list | findstr "Name=" >nul
if not errorlevel 1 (
    echo [OK] CPU information retrieved successfully
)

REM Detect CPU cores
for /f "tokens=2 delims==" %%i in ('wmic cpu get NumberOfCores /format:list ^| findstr "NumberOfCores="') do set CPU_CORES=%%i
echo [OK] Detected %CPU_CORES% CPU cores

REM Detect logical processors
for /f "tokens=2 delims==" %%i in ('wmic cpu get NumberOfLogicalProcessors /format:list ^| findstr "NumberOfLogicalProcessors="') do set LOGICAL_CORES=%%i
echo [OK] Detected %LOGICAL_CORES% logical processors
echo.

REM Create build directory
echo Preparing build environment...
if exist build (
    echo Cleaning old build directory...
    rmdir /s /q build
)

mkdir build
cd build

REM Configure build
echo Configuring CMake...
cmake .. -G "Visual Studio 16 2019" -A x64 -DCMAKE_BUILD_TYPE=Release

if errorlevel 1 (
    echo Error: CMake configuration failed
    echo Trying alternative generator...
    cmake .. -G "Visual Studio 15 2017" -A x64 -DCMAKE_BUILD_TYPE=Release
    if errorlevel 1 (
        echo Error: CMake configuration failed, please check Visual Studio installation
        pause
        exit /b 1
    )
)

echo [OK] CMake configuration completed
echo.

REM Build
echo Starting compilation...
echo Using %LOGICAL_CORES% parallel tasks...

cmake --build . --config Release --parallel %LOGICAL_CORES%

if errorlevel 1 (
    echo Error: Compilation failed
    pause
    exit /b 1
)

echo [OK] Compilation completed
echo.

REM Check generated executable
if exist "Release\pcminer.exe" (
    echo [OK] Executable generated successfully: build\Release\pcminer.exe
    
    REM Show file information
    echo.
    echo Executable information:
    dir Release\pcminer.exe
    
    REM Run basic test
    echo.
    echo Running basic test...
    Release\pcminer.exe --help >nul 2>&1
    if not errorlevel 1 (
        echo [OK] Program runs correctly
    ) else (
        echo [!] Program test failed
    )
    
    echo.
    echo === Build Completed ===
    echo Executable location: %cd%\Release\pcminer.exe
    echo.
    echo Usage:
    echo   pcminer.exe --help                    # Show help
    echo   pcminer.exe --benchmark               # Run performance test
    echo   pcminer.exe -p pool.com -P 3333 -u user  # Start mining
    echo.
    echo Recommended configuration:
    echo   Thread count: %CPU_CORES% (CPU cores)
    echo   Batch size: 1000000
    echo   AVX2 optimization: Auto-detect
    echo.
    
    REM Copy to root directory for convenience
    copy Release\pcminer.exe ..\pcminer.exe >nul
    if not errorlevel 1 (
        echo [OK] Executable copied to root directory
    )
    
) else (
    echo Error: Generated executable not found
    pause
    exit /b 1
)

echo.
echo Press any key to exit...
pause >nul 