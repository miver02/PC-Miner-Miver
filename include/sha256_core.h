#pragma once

#include <cstdint>
#include <cstring>

// Compiler compatibility macro definitions
#ifdef _MSC_VER
    #include <intrin.h>
    #include <immintrin.h>
    #define FORCE_INLINE __forceinline
    #define ALIGN(x) __declspec(align(x))
    #define PREFETCH(addr) _mm_prefetch((const char*)(addr), _MM_HINT_T0)
#else
    #include <immintrin.h>
    #define FORCE_INLINE __attribute__((always_inline)) inline
    #define ALIGN(x) __attribute__((aligned(x)))
    #define PREFETCH(addr) __builtin_prefetch(addr, 0, 3)
#endif

// SHA256 constants
extern const uint32_t K[64];

// SHA256 context structure
struct ALIGN(64) SHA256Context {
    ALIGN(64) uint8_t buffer[64];
    ALIGN(32) uint32_t digest[8];
    uint64_t total_length;
    
    SHA256Context() : total_length(0) {
        // Initialize SHA256 state
        digest[0] = 0x6A09E667;
        digest[1] = 0xBB67AE85;
        digest[2] = 0x3C6EF372;
        digest[3] = 0xA54FF53A;
        digest[4] = 0x510E527F;
        digest[5] = 0x9B05688C;
        digest[6] = 0x1F83D9AB;
        digest[7] = 0x5BE0CD19;
    }
};

// Batch processing result structure
struct BatchResult {
    uint32_t nonce;
    uint8_t hash[32];
    double difficulty;
    bool is_valid;
};

// Core SHA256 function declarations
namespace SHA256Core {
    
    // Basic SHA256 functions
    void sha256_transform(uint32_t state[8], const uint8_t block[64]);
    void sha256_midstate(uint32_t* digest, const uint8_t* data);
    bool sha256_double_hash(const uint32_t* midstate, const uint8_t* data, uint8_t* result);
    
    // Optimized versions
    void sha256_transform_optimized(uint32_t state[8], const uint8_t block[64]);
    void sha256_midstate_optimized(uint32_t* digest, const uint8_t* data);
    bool sha256_double_hash_optimized(const uint32_t* midstate, const uint8_t* data, uint8_t* result);
    
    // Batch processing versions
    bool sha256_batch_process(
        const SHA256Context* context,
        const uint8_t* block_template,
        uint32_t start_nonce,
        uint32_t batch_size,
        BatchResult* results,
        uint32_t* found_count,
        const uint8_t* target
    );
    
    // Ultra high performance batch processing version
    bool sha256_batch_ultra_fast(
        const SHA256Context* context,
        const uint8_t* block_template,
        uint32_t start_nonce,
        uint32_t batch_size,
        BatchResult* results,
        uint32_t* found_count,
        const uint8_t* target
    );
    
    // SIMD optimized version (AVX2)
    #ifdef __AVX2__
    bool sha256_batch_avx2(
        const SHA256Context* context,
        const uint8_t* block_template,
        uint32_t start_nonce,
        uint32_t batch_size,
        BatchResult* results,
        uint32_t* found_count,
        const uint8_t* target
    );
    #endif
    
    // Multithreaded batch processing
    bool sha256_batch_multithreaded(
        const SHA256Context* context,
        const uint8_t* block_template,
        uint32_t start_nonce,
        uint32_t batch_size,
        BatchResult* results,
        uint32_t* found_count,
        const uint8_t* target,
        int num_threads
    );
    
    // Batch size optimization
    uint32_t calculate_optimal_batch_size(double current_hashrate, int num_threads);
    
    // Utility functions
    FORCE_INLINE uint32_t rotr32(uint32_t x, int n);
    FORCE_INLINE uint32_t ch(uint32_t x, uint32_t y, uint32_t z);
    FORCE_INLINE uint32_t maj(uint32_t x, uint32_t y, uint32_t z);
    FORCE_INLINE uint32_t sigma0(uint32_t x);
    FORCE_INLINE uint32_t sigma1(uint32_t x);
    FORCE_INLINE uint32_t gamma0(uint32_t x);
    FORCE_INLINE uint32_t gamma1(uint32_t x);
    
    // Byte order conversion
    FORCE_INLINE uint32_t bswap32(uint32_t x);
    void byte_reverse_words(uint32_t* out, const uint32_t* in, size_t count);
    
    // Difficulty check
    bool check_target(const uint8_t* hash, const uint8_t* target);
    double calculate_difficulty(const uint8_t* hash);
    
    // Performance testing
    void benchmark_sha256();
    void benchmark_batch_processing();
}

// Inline function implementation
namespace SHA256Core {
    
    FORCE_INLINE uint32_t rotr32(uint32_t x, int n) {
        return (x >> n) | (x << (32 - n));
    }
    
    FORCE_INLINE uint32_t ch(uint32_t x, uint32_t y, uint32_t z) {
        return (x & y) ^ (~x & z);
    }
    
    FORCE_INLINE uint32_t maj(uint32_t x, uint32_t y, uint32_t z) {
        return (x & y) ^ (x & z) ^ (y & z);
    }
    
    FORCE_INLINE uint32_t sigma0(uint32_t x) {
        return rotr32(x, 2) ^ rotr32(x, 13) ^ rotr32(x, 22);
    }
    
    FORCE_INLINE uint32_t sigma1(uint32_t x) {
        return rotr32(x, 6) ^ rotr32(x, 11) ^ rotr32(x, 25);
    }
    
    FORCE_INLINE uint32_t gamma0(uint32_t x) {
        return rotr32(x, 7) ^ rotr32(x, 18) ^ (x >> 3);
    }
    
    FORCE_INLINE uint32_t gamma1(uint32_t x) {
        return rotr32(x, 17) ^ rotr32(x, 19) ^ (x >> 10);
    }
    
    FORCE_INLINE uint32_t bswap32(uint32_t x) {
#ifdef _MSC_VER
        return _byteswap_ulong(x);
#else
        return __builtin_bswap32(x);
#endif
    }
} 