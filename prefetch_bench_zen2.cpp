/*
 * RandomX Prefetch Strategy Benchmark for AMD Zen 2
 * Compiled: g++ -O3 -march=znver2 -o prefetch_bench prefetch_bench_zen2.cpp
 *
 * Tests different prefetch strategies to find optimal configuration
 * for Ryzen 9 3950X (Zen 2 architecture)
 */

#include <iostream>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <random>
#include <vector>
#include <x86intrin.h>

constexpr size_t SCRATCHPAD_SIZE = 2 * 1024 * 1024;  // 2MB (RandomX size)
constexpr size_t DATASET_SIZE = 256 * 1024 * 1024;   // 256MB subset
constexpr size_t CACHE_LINE = 64;
constexpr size_t ITERATIONS = 10000000;

enum PrefetchStrategy {
    NO_PREFETCH = 0,
    PREFETCH_T0,      // All cache levels
    PREFETCH_T1,      // L2 and L3
    PREFETCH_T2,      // L3 only
    PREFETCH_NTA,     // Non-temporal (bypass L1/L2)
    PREFETCH_W        // Prefetch for write
};

const char* strategy_names[] = {
    "No Prefetch",
    "PREFETCHT0 (L1/L2/L3)",
    "PREFETCHT1 (L2/L3)",
    "PREFETCHT2 (L3)",
    "PREFETCHNTA (Non-temporal)",
    "PREFETCHW (Write hint)"
};

// Align to cache line to avoid false sharing
alignas(64) uint8_t scratchpad[SCRATCHPAD_SIZE];
alignas(64) uint8_t dataset[DATASET_SIZE];

// Inline assembly for different prefetch types
inline void prefetch_t0(const void* addr) {
    __builtin_prefetch(addr, 0, 3);  // Read, high temporal locality
}

inline void prefetch_t1(const void* addr) {
    __builtin_prefetch(addr, 0, 2);  // Read, moderate temporal locality
}

inline void prefetch_t2(const void* addr) {
    __builtin_prefetch(addr, 0, 1);  // Read, low temporal locality
}

inline void prefetch_nta(const void* addr) {
    __builtin_prefetch(addr, 0, 0);  // Read, non-temporal
}

inline void prefetch_w(const void* addr) {
    __builtin_prefetch(addr, 1, 3);  // Write, high temporal locality
}

// Simulate RandomX memory access pattern
template<PrefetchStrategy strategy>
uint64_t benchmark_pattern(size_t iterations, int prefetch_distance) {
    uint64_t result = 0x123456789ABCDEFULL;

    // Prefetch distance in cache lines
    const size_t prefetch_offset = prefetch_distance * CACHE_LINE;

    auto start = std::chrono::high_resolution_clock::now();

    for (size_t i = 0; i < iterations; i++) {
        // Simulate RandomX register state
        uint32_t ma = static_cast<uint32_t>(result);
        uint32_t mx = static_cast<uint32_t>(result >> 32);

        // Scratchpad access (L3-resident, random pattern)
        size_t spAddr = (ma & (SCRATCHPAD_SIZE - 1)) & ~63;

        // Dataset access (DRAM-backed, sequential-ish)
        size_t dsAddr = (mx & (DATASET_SIZE - 1)) & ~63;

        // Prefetch next access based on strategy
        if constexpr (strategy == PREFETCH_T0) {
            size_t nextAddr = ((ma + prefetch_offset) & (SCRATCHPAD_SIZE - 1)) & ~63;
            prefetch_t0(&scratchpad[nextAddr]);
            size_t nextDsAddr = ((mx + prefetch_offset) & (DATASET_SIZE - 1)) & ~63;
            prefetch_t0(&dataset[nextDsAddr]);
        } else if constexpr (strategy == PREFETCH_T1) {
            size_t nextAddr = ((ma + prefetch_offset) & (SCRATCHPAD_SIZE - 1)) & ~63;
            prefetch_t1(&scratchpad[nextAddr]);
            size_t nextDsAddr = ((mx + prefetch_offset) & (DATASET_SIZE - 1)) & ~63;
            prefetch_t1(&dataset[nextDsAddr]);
        } else if constexpr (strategy == PREFETCH_T2) {
            size_t nextAddr = ((ma + prefetch_offset) & (SCRATCHPAD_SIZE - 1)) & ~63;
            prefetch_t2(&scratchpad[nextAddr]);
            size_t nextDsAddr = ((mx + prefetch_offset) & (DATASET_SIZE - 1)) & ~63;
            prefetch_t2(&dataset[nextDsAddr]);
        } else if constexpr (strategy == PREFETCH_NTA) {
            size_t nextAddr = ((ma + prefetch_offset) & (SCRATCHPAD_SIZE - 1)) & ~63;
            prefetch_nta(&scratchpad[nextAddr]);
            size_t nextDsAddr = ((mx + prefetch_offset) & (DATASET_SIZE - 1)) & ~63;
            prefetch_nta(&dataset[nextDsAddr]);
        } else if constexpr (strategy == PREFETCH_W) {
            size_t nextAddr = ((ma + prefetch_offset) & (SCRATCHPAD_SIZE - 1)) & ~63;
            prefetch_w(&scratchpad[nextAddr]);
        }

        // Load from scratchpad (simulates XOR operations in RandomX)
        uint64_t* sp_ptr = reinterpret_cast<uint64_t*>(&scratchpad[spAddr]);
        result ^= sp_ptr[0];
        result ^= sp_ptr[1];
        result ^= sp_ptr[2];
        result ^= sp_ptr[3];

        // Load from dataset
        uint64_t* ds_ptr = reinterpret_cast<uint64_t*>(&dataset[dsAddr]);
        result ^= ds_ptr[0];
        result ^= ds_ptr[1];

        // Simulate computation (to prevent over-optimization)
        result = (result * 6364136223846793005ULL) + 1442695040888963407ULL;
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();

    // Prevent result from being optimized away
    volatile uint64_t dummy = result;
    (void)dummy;

    return duration;
}

void run_benchmark(const char* name, PrefetchStrategy strategy, int prefetch_distance) {
    std::cout << "\n=== " << name << " (distance: " << prefetch_distance << " cache lines) ===" << std::endl;

    // Warmup
    if (strategy == NO_PREFETCH) {
        benchmark_pattern<NO_PREFETCH>(ITERATIONS / 10, prefetch_distance);
    } else if (strategy == PREFETCH_T0) {
        benchmark_pattern<PREFETCH_T0>(ITERATIONS / 10, prefetch_distance);
    } else if (strategy == PREFETCH_T1) {
        benchmark_pattern<PREFETCH_T1>(ITERATIONS / 10, prefetch_distance);
    } else if (strategy == PREFETCH_T2) {
        benchmark_pattern<PREFETCH_T2>(ITERATIONS / 10, prefetch_distance);
    } else if (strategy == PREFETCH_NTA) {
        benchmark_pattern<PREFETCH_NTA>(ITERATIONS / 10, prefetch_distance);
    } else if (strategy == PREFETCH_W) {
        benchmark_pattern<PREFETCH_W>(ITERATIONS / 10, prefetch_distance);
    }

    // Actual benchmark - run 3 times and take best
    uint64_t best_time = UINT64_MAX;
    for (int run = 0; run < 3; run++) {
        uint64_t time;
        if (strategy == NO_PREFETCH) {
            time = benchmark_pattern<NO_PREFETCH>(ITERATIONS, prefetch_distance);
        } else if (strategy == PREFETCH_T0) {
            time = benchmark_pattern<PREFETCH_T0>(ITERATIONS, prefetch_distance);
        } else if (strategy == PREFETCH_T1) {
            time = benchmark_pattern<PREFETCH_T1>(ITERATIONS, prefetch_distance);
        } else if (strategy == PREFETCH_T2) {
            time = benchmark_pattern<PREFETCH_T2>(ITERATIONS, prefetch_distance);
        } else if (strategy == PREFETCH_NTA) {
            time = benchmark_pattern<PREFETCH_NTA>(ITERATIONS, prefetch_distance);
        } else if (strategy == PREFETCH_W) {
            time = benchmark_pattern<PREFETCH_W>(ITERATIONS, prefetch_distance);
        }

        if (time < best_time) {
            best_time = time;
        }

        std::cout << "  Run " << (run + 1) << ": " << time << " us" << std::endl;
    }

    double ns_per_iter = (best_time * 1000.0) / ITERATIONS;
    std::cout << "  Best: " << best_time << " us (" << ns_per_iter << " ns/iter)" << std::endl;
}

int main() {
    std::cout << "RandomX Prefetch Benchmark for AMD Zen 2 (Ryzen 9 3950X)" << std::endl;
    std::cout << "Scratchpad: " << (SCRATCHPAD_SIZE / 1024) << " KB" << std::endl;
    std::cout << "Dataset: " << (DATASET_SIZE / 1024 / 1024) << " MB" << std::endl;
    std::cout << "Iterations: " << ITERATIONS << std::endl;
    std::cout << "\nInitializing memory..." << std::endl;

    // Initialize with random data
    std::mt19937_64 rng(0x123456789ABCDEFULL);
    for (size_t i = 0; i < SCRATCHPAD_SIZE / 8; i++) {
        reinterpret_cast<uint64_t*>(scratchpad)[i] = rng();
    }
    for (size_t i = 0; i < DATASET_SIZE / 8; i++) {
        reinterpret_cast<uint64_t*>(dataset)[i] = rng();
    }

    std::cout << "\nStarting benchmark...\n" << std::endl;

    // Test baseline
    run_benchmark(strategy_names[NO_PREFETCH], NO_PREFETCH, 0);

    // Test different strategies with various prefetch distances
    int distances[] = {4, 5, 6, 8, 10};  // In cache lines (256, 320, 384, 512, 640 bytes)

    for (int dist : distances) {
        run_benchmark(strategy_names[PREFETCH_T0], PREFETCH_T0, dist);
    }

    for (int dist : distances) {
        run_benchmark(strategy_names[PREFETCH_T1], PREFETCH_T1, dist);
    }

    for (int dist : distances) {
        run_benchmark(strategy_names[PREFETCH_T2], PREFETCH_T2, dist);
    }

    for (int dist : distances) {
        run_benchmark(strategy_names[PREFETCH_NTA], PREFETCH_NTA, dist);
    }

    // Test write prefetch for scratchpad writes
    for (int dist : distances) {
        run_benchmark(strategy_names[PREFETCH_W], PREFETCH_W, dist);
    }

    std::cout << "\n=== Benchmark Complete ===" << std::endl;
    std::cout << "\nRecommendations:" << std::endl;
    std::cout << "- For L3-resident data (scratchpad): Likely PREFETCHT0 or PREFETCHT1" << std::endl;
    std::cout << "- For DRAM-resident data (dataset): Likely PREFETCHNTA or PREFETCHT2" << std::endl;
    std::cout << "- Optimal distance on Zen 2 is typically 5-6 cache lines (320-384 bytes)" << std::endl;

    return 0;
}
