#!/bin/bash
# Build script for Zen 2 optimized XMRig
# For Ryzen 9 3950X and other Zen 2 CPUs

set -e

echo "==================================="
echo "XMRig Zen 2 Optimization Build"
echo "==================================="
echo ""

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Detect CPU
CPU_MODEL=$(lscpu | grep "Model name" | cut -d: -f2 | xargs)
CPU_FAMILY=$(lscpu | grep "CPU family" | cut -d: -f2 | xargs)
CPU_MODEL_ID=$(lscpu | grep "Model:" | cut -d: -f2 | xargs)

echo "Detected CPU: $CPU_MODEL"
echo "Family: $CPU_FAMILY, Model: $CPU_MODEL_ID"
echo ""

# Check if Zen 2
if [ "$CPU_FAMILY" != "23" ]; then
    echo -e "${YELLOW}Warning: This script is optimized for AMD Zen 2 (Family 23)${NC}"
    echo -e "${YELLOW}Your CPU is Family $CPU_FAMILY. Continue? (y/n)${NC}"
    read -r response
    if [ "$response" != "y" ]; then
        exit 1
    fi
fi

# Check for required tools
echo "Checking dependencies..."
command -v cmake >/dev/null 2>&1 || { echo -e "${RED}Error: cmake not found${NC}"; exit 1; }
command -v g++ >/dev/null 2>&1 || { echo -e "${RED}Error: g++ not found${NC}"; exit 1; }
echo -e "${GREEN}✓ Dependencies OK${NC}"
echo ""

# Build prefetch benchmark first
echo "=== Building Prefetch Benchmark ==="
if [ -f "prefetch_bench_zen2.cpp" ]; then
    g++ -O3 -march=znver2 -lpthread -o prefetch_bench prefetch_bench_zen2.cpp
    echo -e "${GREEN}✓ Prefetch benchmark built: ./prefetch_bench${NC}"
    echo ""

    echo "Run benchmark now? (y/n)"
    read -r run_bench
    if [ "$run_bench" = "y" ]; then
        ./prefetch_bench
        echo ""
        echo "Press Enter to continue with XMRig build..."
        read
    fi
else
    echo -e "${YELLOW}Warning: prefetch_bench_zen2.cpp not found, skipping${NC}"
fi

# Ask about prefetch optimization
echo ""
echo "=== Prefetch Strategy ==="
echo "Current: PREFETCHNTA (non-temporal, bypasses L1/L2)"
echo "Proposed: PREFETCHT0 (all cache levels)"
echo ""
echo "Apply Zen 2 prefetch optimization? (y/n)"
echo "(Recommended to test with benchmark first)"
read -r apply_prefetch

if [ "$apply_prefetch" = "y" ]; then
    echo "Backing up original assembly files..."
    cd src/crypto/randomx/asm/

    [ ! -f program_sshash_prefetch.inc.orig ] && cp program_sshash_prefetch.inc program_sshash_prefetch.inc.orig
    [ ! -f program_sshash_avx2_ssh_prefetch.inc.orig ] && cp program_sshash_avx2_ssh_prefetch.inc program_sshash_avx2_ssh_prefetch.inc.orig
    [ ! -f program_read_dataset.inc.orig ] && cp program_read_dataset.inc program_read_dataset.inc.orig

    echo "Applying Zen 2 optimized prefetch..."
    cp program_sshash_prefetch_zen2.inc program_sshash_prefetch.inc
    cp program_sshash_avx2_ssh_prefetch_zen2.inc program_sshash_avx2_ssh_prefetch.inc
    cp program_read_dataset_zen2.inc program_read_dataset.inc

    cd ../../../..
    echo -e "${GREEN}✓ Prefetch optimization applied${NC}"
else
    echo "Using default prefetch strategy"
fi

echo ""
echo "=== Building XMRig ==="

# Create build directory
mkdir -p build
cd build

# Build type
echo ""
echo "Build type:"
echo "1) Release (default)"
echo "2) Release with LTO (link-time optimization, slower build, ~2% faster)"
echo "3) RelWithDebInfo (for profiling)"
read -p "Choice [1]: " build_type
build_type=${build_type:-1}

CMAKE_BUILD_TYPE="Release"
EXTRA_FLAGS=""

case $build_type in
    2)
        CMAKE_BUILD_TYPE="Release"
        EXTRA_FLAGS="-flto"
        echo "Using LTO..."
        ;;
    3)
        CMAKE_BUILD_TYPE="RelWithDebInfo"
        echo "Using RelWithDebInfo..."
        ;;
    *)
        echo "Using Release..."
        ;;
esac

# CMake configuration
echo ""
echo "Running CMake..."
cmake .. \
    -DCMAKE_BUILD_TYPE=$CMAKE_BUILD_TYPE \
    -DCMAKE_C_FLAGS="-march=znver2 -mtune=znver2 $EXTRA_FLAGS" \
    -DCMAKE_CXX_FLAGS="-march=znver2 -mtune=znver2 $EXTRA_FLAGS" \
    -DCMAKE_EXE_LINKER_FLAGS="$EXTRA_FLAGS" \
    -DWITH_HWLOC=ON \
    -DWITH_MSR=ON \
    -DWITH_ASM=ON \
    || { echo -e "${RED}CMake failed${NC}"; exit 1; }

echo -e "${GREEN}✓ CMake configuration complete${NC}"

# Build
echo ""
echo "Building (using $(nproc) threads)..."
make -j$(nproc) || { echo -e "${RED}Build failed${NC}"; exit 1; }

echo ""
echo -e "${GREEN}==================================="
echo "✓ Build Complete!"
echo "===================================${NC}"
echo ""
echo "Binary: $(pwd)/xmrig"
echo "Version: $(./xmrig --version | head -1)"
echo ""

# Check optimizations
echo "Checking build features..."
./xmrig --version | grep -E "ASM|AVX2|MSR|hwloc"
echo ""

# Create sample config
if [ ! -f "config.json" ]; then
    echo "Creating sample config.json..."
    cat > config.json <<EOF
{
    "autosave": true,
    "cpu": {
        "enabled": true,
        "huge-pages": true,
        "huge-pages-jit": true,
        "hw-aes": true,
        "priority": 5,
        "memory-pool": false,
        "asm": true,
        "max-threads-hint": 100
    },
    "randomx": {
        "init": -1,
        "init-avx2": 1,
        "mode": "fast",
        "1gb-pages": true,
        "wrmsr": true,
        "cache_qos": true,
        "scratchpad_prefetch_mode": 1,
        "numa": true
    },
    "pools": [
        {
            "url": "pool.example.com:3333",
            "user": "YOUR_WALLET_ADDRESS",
            "pass": "x",
            "keepalive": true,
            "tls": false
        }
    ]
}
EOF
    echo -e "${GREEN}✓ Created config.json${NC}"
    echo -e "${YELLOW}  Remember to edit pool settings!${NC}"
fi

echo ""
echo "=== Next Steps ==="
echo "1. Edit config.json with your pool settings"
echo "2. Enable huge pages (see ZEN2_OPTIMIZATION_GUIDE.md)"
echo "3. Load MSR module: sudo modprobe msr"
echo "4. Set CPU governor: sudo cpupower frequency-set -g performance"
echo "5. Run: ./xmrig --config=config.json"
echo ""
echo "For detailed optimization guide, see: ../ZEN2_OPTIMIZATION_GUIDE.md"
echo ""

# Offer to run benchmark
echo "Run quick benchmark now? (y/n)"
read -r run_test
if [ "$run_test" = "y" ]; then
    echo ""
    echo "Running 1M benchmark..."
    ./xmrig --bench=1M
fi

echo ""
echo "Done!"
