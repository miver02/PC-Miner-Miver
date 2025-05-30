@echo off
chcp 65001 >nul
setlocal enabledelayedexpansion

REM PC Mining Program Build Script
REM For Windows Systems

echo === PC Mining Program Build Script ===
echo.

REM Setup Visual Studio environment first
echo Setting up Visual Studio environment...
call "D:\Download\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul 2>&1
if errorlevel 1 (
    echo Error: Failed to setup Visual Studio environment
    echo Please check Visual Studio installation path
    pause
    exit /b 1
)
echo [OK] Visual Studio environment initialized

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

REM Check vcpkg (optional)
set USE_VCPKG=0
if exist "vcpkg\scripts\buildsystems\vcpkg.cmake" (
    echo [OK] vcpkg found, will use vcpkg toolchain   
    set USE_VCPKG=1
) else (
    echo [NO] vcpkg not found, trying manual OpenSSL detection
    REM Try to set OpenSSL path manually
    if exist "C:\Program Files\OpenSSL-Win64" (
        set OPENSSL_ROOT_DIR=C:\Program Files\OpenSSL-Win64
        echo [OK] Found OpenSSL at: %OPENSSL_ROOT_DIR%
    ) else (
        echo [NO] OpenSSL not found. You may need to:
        echo     1. Install OpenSSL manually, or
        echo     2. Run install_dependencies.bat to install vcpkg
        echo.
        echo Continuing anyway, some features may not work...
    )
)

echo [OK] Dependencies check completed
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
if %USE_VCPKG%==1 (
    echo Using vcpkg toolchain...
    cmake .. -G "Visual Studio 17 2022" -A x64 -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=..\vcpkg\scripts\buildsystems\vcpkg.cmake
) else (
    echo Using standard configuration...
    cmake .. -G "Visual Studio 17 2022" -A x64 -DCMAKE_BUILD_TYPE=Release
)

if errorlevel 1 (
    echo Error: CMake configuration failed
    echo Trying without specific OpenSSL settings...
    cmake .. -G "Visual Studio 17 2022" -A x64 -DCMAKE_BUILD_TYPE=Release -DOPENSSL_ROOT_DIR="" -DOpenSSL_FIND_QUIETLY=ON
    if errorlevel 1 (
        echo Error: CMake configuration failed
        echo Please check your CMakeLists.txt and dependencies
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
    echo   Thread count: %CPU_CORES% ^(CPU cores^)
    echo   Batch size: 1000000
    echo   AVX2 optimization: Auto-detect
    echo.
    
    REM Copy to root directory for convenience
    copy Release\pcminer.exe ..\pcminer.exe >nul 2>&1
    if not errorlevel 1 (
        echo [OK] Executable copied to root directory
    ) else (
        echo [WARNING] Failed to copy executable to root directory
    )
    
    echo.
    echo === BUILD SUCCESSFUL ===
    
) else (
    echo Error: Generated executable not found
    pause
    exit /b 1
)

echo.
echo Press any key to exit...
pause >nul 