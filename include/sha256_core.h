#pragma once

#include <cstdint>
#include <cstring>
#include <immintrin.h>

// 性能优化宏定义
#define FORCE_INLINE __forceinline
#define ALIGN(x) __attribute__((aligned(x)))
#define PREFETCH(addr) __builtin_prefetch(addr, 0, 3)

// SHA256常量
extern const uint32_t K[64];

// SHA256上下文结构
struct SHA256Context {
    uint8_t buffer[64] ALIGN(64);
    uint32_t digest[8] ALIGN(32);
    uint64_t total_length;
    
    SHA256Context() : total_length(0) {
        // 初始化SHA256状态
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

// 批处理结果结构
struct BatchResult {
    uint32_t nonce;
    uint8_t hash[32];
    double difficulty;
    bool is_valid;
};

// 核心SHA256函数声明
namespace SHA256Core {
    
    // 基础SHA256函数
    void sha256_transform(uint32_t state[8], const uint8_t block[64]);
    void sha256_midstate(uint32_t* digest, const uint8_t* data);
    bool sha256_double_hash(const uint32_t* midstate, const uint8_t* data, uint8_t* result);
    
    // 优化版本
    FORCE_INLINE void sha256_transform_optimized(uint32_t state[8], const uint8_t block[64]);
    FORCE_INLINE void sha256_midstate_optimized(uint32_t* digest, const uint8_t* data);
    FORCE_INLINE bool sha256_double_hash_optimized(const uint32_t* midstate, const uint8_t* data, uint8_t* result);
    
    // 批处理版本
    bool sha256_batch_process(
        const SHA256Context* context,
        const uint8_t* block_template,
        uint32_t start_nonce,
        uint32_t batch_size,
        BatchResult* results,
        uint32_t* found_count,
        const uint8_t* target
    );
    
    // 超高性能批处理版本
    bool sha256_batch_ultra_fast(
        const SHA256Context* context,
        const uint8_t* block_template,
        uint32_t start_nonce,
        uint32_t batch_size,
        BatchResult* results,
        uint32_t* found_count,
        const uint8_t* target
    );
    
    // SIMD优化版本 (AVX2)
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
    
    // 工具函数
    FORCE_INLINE uint32_t rotr32(uint32_t x, int n);
    FORCE_INLINE uint32_t ch(uint32_t x, uint32_t y, uint32_t z);
    FORCE_INLINE uint32_t maj(uint32_t x, uint32_t y, uint32_t z);
    FORCE_INLINE uint32_t sigma0(uint32_t x);
    FORCE_INLINE uint32_t sigma1(uint32_t x);
    FORCE_INLINE uint32_t gamma0(uint32_t x);
    FORCE_INLINE uint32_t gamma1(uint32_t x);
    
    // 字节序转换
    FORCE_INLINE uint32_t bswap32(uint32_t x);
    void byte_reverse_words(uint32_t* out, const uint32_t* in, size_t count);
    
    // 难度检查
    bool check_target(const uint8_t* hash, const uint8_t* target);
    double calculate_difficulty(const uint8_t* hash);
    
    // 性能测试
    void benchmark_sha256();
    void benchmark_batch_processing();
}

// 内联函数实现
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
        return __builtin_bswap32(x);
    }
} 