#!/bin/bash

# PC Miner Build Script for Linux
# Optimized for performance and compatibility

set -e  # Exit on error

echo "=== PC Miner Build Script ==="
echo

# Check system dependencies
echo "Checking system dependencies..."

# Check CMake
if ! command -v cmake &> /dev/null; then
    echo "Error: CMake not found. Please install CMake 3.16+"
    echo "Ubuntu/Debian: sudo apt-get install cmake"
    echo "CentOS/RHEL: sudo yum install cmake"
    echo "Arch Linux: sudo pacman -S cmake"
    exit 1
fi

CMAKE_VERSION=$(cmake --version | head -n1 | cut -d' ' -f3)
echo "✓ CMake version: $CMAKE_VERSION"

# Check compiler
if ! command -v g++ &> /dev/null; then
    echo "Error: G++ compiler not found. Please install build tools"
    echo "Ubuntu/Debian: sudo apt-get install build-essential"
    echo "CentOS/RHEL: sudo yum groupinstall 'Development Tools'"
    echo "Arch Linux: sudo pacman -S base-devel"
    exit 1
fi

GCC_VERSION=$(g++ --version | head -n1 | cut -d' ' -f4)
echo "✓ G++ version: $GCC_VERSION"

# Check OpenSSL development libraries
if ! pkg-config --exists openssl; then
    echo "Error: OpenSSL development libraries not found"
    echo "Ubuntu/Debian: sudo apt-get install libssl-dev"
    echo "CentOS/RHEL: sudo yum install openssl-devel"
    echo "Arch Linux: sudo pacman -S openssl"
    exit 1
fi

OPENSSL_VERSION=$(pkg-config --modversion openssl)
echo "✓ OpenSSL version: $OPENSSL_VERSION"

echo "✓ All dependencies check passed"
echo

# Detect CPU features
echo "Detecting CPU features..."
if grep -q avx2 /proc/cpuinfo; then
    echo "✓ AVX2 support detected"
    AVX2_SUPPORT=ON
else
    echo "! AVX2 not detected, using basic optimizations"
    AVX2_SUPPORT=OFF
fi

if grep -q sse2 /proc/cpuinfo; then
    echo "✓ SSE2 support detected"
else
    echo "! SSE2 not detected, performance may be limited"
fi

CPU_CORES=$(nproc)
echo "✓ Detected $CPU_CORES CPU cores"
echo

# Prepare build environment
echo "Preparing build environment..."
if [ -d "build" ]; then
    echo "Cleaning old build directory..."
    rm -rf build
fi

mkdir build
cd build

# Configure build
echo "Configuring CMake..."
cmake .. \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_CXX_STANDARD=17

if [ $? -ne 0 ]; then
    echo "Error: CMake configuration failed"
    exit 1
fi

echo "✓ CMake configuration completed"
echo

# Compile
echo "Starting compilation..."
echo "Using $CPU_CORES parallel jobs..."

make -j$CPU_CORES

if [ $? -ne 0 ]; then
    echo "Error: Compilation failed"
    exit 1
fi

echo "✓ Compilation completed"
echo

# Check generated executable
if [ -f "pcminer" ]; then
    echo "✓ Executable generated successfully: build/pcminer"
    
    # Show file information
    echo
    echo "Executable information:"
    ls -lh pcminer
    file pcminer
    
    # Run basic test
    echo
    echo "Running basic test..."
    if ./pcminer --help > /dev/null 2>&1; then
        echo "✓ Program runs normally"
    else
        echo "! Program test failed"
    fi
    
    echo
    echo "=== Build Completed ==="
    echo "Executable location: $(pwd)/pcminer"
    echo
    echo "Usage:"
    echo "  ./pcminer --help                    # Show help"
    echo "  ./pcminer --benchmark               # Run performance test"
    echo "  ./pcminer -p pool.com -P 3333 -u user  # Start mining"
    echo
    echo "Recommended configuration:"
    echo "  Threads: $CPU_CORES (CPU cores)"
    echo "  Batch size: 1000"
    if [ "$AVX2_SUPPORT" = "ON" ]; then
        echo "  AVX2 optimization: Enabled"
    else
        echo "  AVX2 optimization: Disabled"
    fi
    echo
    echo "To copy configuration file:"
    echo "  cp ../miner.conf.example ./miner.conf"
    echo "  nano miner.conf  # Edit configuration"
    
else
    echo "Error: Generated executable not found"
    exit 1
fi 