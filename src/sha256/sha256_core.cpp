#include "sha256_core.h"
#include <iostream>
#include <chrono>
#include <cstring>
#include <algorithm>

// SHA256 constants K
const uint32_t K[64] = {
    0x428A2F98, 0x71374491, 0xB5C0FBCF, 0xE9B5DBA5, 0x3956C25B, 0x59F111F1, 0x923F82A4, 0xAB1C5ED5,
    0xD807AA98, 0x12835B01, 0x243185BE, 0x550C7DC3, 0x72BE5D74, 0x80DEB1FE, 0x9BDC06A7, 0xC19BF174,
    0xE49B69C1, 0xEFBE4786, 0x0FC19DC6, 0x240CA1CC, 0x2DE92C6F, 0x4A7484AA, 0x5CB0A9DC, 0x76F988DA,
    0x983E5152, 0xA831C66D, 0xB00327C8, 0xBF597FC7, 0xC6E00BF3, 0xD5A79147, 0x06CA6351, 0x14292967,
    0x27B70A85, 0x2E1B2138, 0x4D2C6DFC, 0x53380D13, 0x650A7354, 0x766A0ABB, 0x81C2C92E, 0x92722C85,
    0xA2BFE8A1, 0xA81A664B, 0xC24B8B70, 0xC76C51A3, 0xD192E819, 0xD6990624, 0xF40E3585, 0x106AA070,
    0x19A4C116, 0x1E376C08, 0x2748774C, 0x34B0BCB5, 0x391C0CB3, 0x4ED8AA4A, 0x5B9CCA4F, 0x682E6FF3,
    0x748F82EE, 0x78A5636F, 0x84C87814, 0x8CC70208, 0x90BEFFFA, 0xA4506CEB, 0xBEF9A3F7, 0xC67178F2
};

namespace SHA256Core {

// Basic SHA256 transform function
void sha256_transform(uint32_t state[8], const uint8_t block[64]) {
    uint32_t W[64];
    uint32_t a, b, c, d, e, f, g, h;
    uint32_t temp1, temp2;
    
    // Prepare message schedule array
    for (int i = 0; i < 16; i++) {
        W[i] = bswap32(((uint32_t*)block)[i]);
    }
    
    for (int i = 16; i < 64; i++) {
        W[i] = gamma1(W[i-2]) + W[i-7] + gamma0(W[i-15]) + W[i-16];
    }
    
    // Initialize working variables
    a = state[0]; b = state[1]; c = state[2]; d = state[3];
    e = state[4]; f = state[5]; g = state[6]; h = state[7];
    
    // Main loop
    for (int i = 0; i < 64; i++) {
        temp1 = h + sigma1(e) + ch(e, f, g) + K[i] + W[i];
        temp2 = sigma0(a) + maj(a, b, c);
        h = g; g = f; f = e; e = d + temp1;
        d = c; c = b; b = a; a = temp1 + temp2;
    }
    
    // Update state
    state[0] += a; state[1] += b; state[2] += c; state[3] += d;
    state[4] += e; state[5] += f; state[6] += g; state[7] += h;
}

// Optimized SHA256 transform - manual loop unrolling
void sha256_transform_optimized(uint32_t state[8], const uint8_t block[64]) {
    ALIGN(32) uint32_t W[64];
    uint32_t a, b, c, d, e, f, g, h;
    
    // Prefetch data
    PREFETCH(block);
    PREFETCH(block + 32);
    
    // Prepare message schedule array - unroll first 16
    const uint32_t* block32 = (const uint32_t*)block;
    W[0] = bswap32(block32[0]);   W[1] = bswap32(block32[1]);
    W[2] = bswap32(block32[2]);   W[3] = bswap32(block32[3]);
    W[4] = bswap32(block32[4]);   W[5] = bswap32(block32[5]);
    W[6] = bswap32(block32[6]);   W[7] = bswap32(block32[7]);
    W[8] = bswap32(block32[8]);   W[9] = bswap32(block32[9]);
    W[10] = bswap32(block32[10]); W[11] = bswap32(block32[11]);
    W[12] = bswap32(block32[12]); W[13] = bswap32(block32[13]);
    W[14] = bswap32(block32[14]); W[15] = bswap32(block32[15]);
    
    // Extend message schedule array - partially unroll
    for (int i = 16; i < 64; i += 4) {
        W[i]   = gamma1(W[i-2])   + W[i-7]   + gamma0(W[i-15])   + W[i-16];
        W[i+1] = gamma1(W[i-1])   + W[i-6]   + gamma0(W[i-14])   + W[i-15];
        W[i+2] = gamma1(W[i])     + W[i-5]   + gamma0(W[i-13])   + W[i-14];
        W[i+3] = gamma1(W[i+1])   + W[i-4]   + gamma0(W[i-12])   + W[i-13];
    }
    
    // Initialize working variables
    a = state[0]; b = state[1]; c = state[2]; d = state[3];
    e = state[4]; f = state[5]; g = state[6]; h = state[7];
    
    // Main loop - unroll 8 rounds
    for (int i = 0; i < 64; i += 8) {
        uint32_t temp1, temp2;
        
        // Round i
        temp1 = h + sigma1(e) + ch(e, f, g) + K[i] + W[i];
        temp2 = sigma0(a) + maj(a, b, c);
        h = g; g = f; f = e; e = d + temp1; d = c; c = b; b = a; a = temp1 + temp2;
        
        // Round i+1
        temp1 = h + sigma1(e) + ch(e, f, g) + K[i+1] + W[i+1];
        temp2 = sigma0(a) + maj(a, b, c);
        h = g; g = f; f = e; e = d + temp1; d = c; c = b; b = a; a = temp1 + temp2;
        
        // Round i+2
        temp1 = h + sigma1(e) + ch(e, f, g) + K[i+2] + W[i+2];
        temp2 = sigma0(a) + maj(a, b, c);
        h = g; g = f; f = e; e = d + temp1; d = c; c = b; b = a; a = temp1 + temp2;
        
        // Round i+3
        temp1 = h + sigma1(e) + ch(e, f, g) + K[i+3] + W[i+3];
        temp2 = sigma0(a) + maj(a, b, c);
        h = g; g = f; f = e; e = d + temp1; d = c; c = b; b = a; a = temp1 + temp2;
        
        // Round i+4
        temp1 = h + sigma1(e) + ch(e, f, g) + K[i+4] + W[i+4];
        temp2 = sigma0(a) + maj(a, b, c);
        h = g; g = f; f = e; e = d + temp1; d = c; c = b; b = a; a = temp1 + temp2;
        
        // Round i+5
        temp1 = h + sigma1(e) + ch(e, f, g) + K[i+5] + W[i+5];
        temp2 = sigma0(a) + maj(a, b, c);
        h = g; g = f; f = e; e = d + temp1; d = c; c = b; b = a; a = temp1 + temp2;
        
        // Round i+6
        temp1 = h + sigma1(e) + ch(e, f, g) + K[i+6] + W[i+6];
        temp2 = sigma0(a) + maj(a, b, c);
        h = g; g = f; f = e; e = d + temp1; d = c; c = b; b = a; a = temp1 + temp2;
        
        // Round i+7
        temp1 = h + sigma1(e) + ch(e, f, g) + K[i+7] + W[i+7];
        temp2 = sigma0(a) + maj(a, b, c);
        h = g; g = f; f = e; e = d + temp1; d = c; c = b; b = a; a = temp1 + temp2;
    }
    
    // Update state
    state[0] += a; state[1] += b; state[2] += c; state[3] += d;
    state[4] += e; state[5] += f; state[6] += g; state[7] += h;
}

// Calculate intermediate state
void sha256_midstate(uint32_t* digest, const uint8_t* data) {
    SHA256Context ctx;
    memcpy(digest, ctx.digest, 32);
    sha256_transform_optimized(digest, data);
}

// Optimized intermediate state calculation
void sha256_midstate_optimized(uint32_t* digest, const uint8_t* data) {
    // Initialize SHA256 state
    digest[0] = 0x6A09E667; digest[1] = 0xBB67AE85;
    digest[2] = 0x3C6EF372; digest[3] = 0xA54FF53A;
    digest[4] = 0x510E527F; digest[5] = 0x9B05688C;
    digest[6] = 0x1F83D9AB; digest[7] = 0x5BE0CD19;
    
    sha256_transform_optimized(digest, data);
}

// SHA256 double hash
bool sha256_double_hash(const uint32_t* midstate, const uint8_t* data, uint8_t* result) {
    uint32_t state[8];
    ALIGN(32) uint8_t temp_hash[32];
    ALIGN(64) uint8_t padded_block[64];
    
    // Copy intermediate state
    memcpy(state, midstate, 32);
    
    // First hash
    sha256_transform_optimized(state, data);
    
    // Convert to byte order and prepare second hash
    for (int i = 0; i < 8; i++) {
        ((uint32_t*)temp_hash)[i] = bswap32(state[i]);
    }
    
    // Prepare padding block
    memset(padded_block, 0, 64);
    memcpy(padded_block, temp_hash, 32);
    padded_block[32] = 0x80;
    ((uint32_t*)padded_block)[15] = bswap32(256); // Length is 256 bits
    
    // Reset state for second hash
    state[0] = 0x6A09E667; state[1] = 0xBB67AE85;
    state[2] = 0x3C6EF372; state[3] = 0xA54FF53A;
    state[4] = 0x510E527F; state[5] = 0x9B05688C;
    state[6] = 0x1F83D9AB; state[7] = 0x5BE0CD19;
    
    sha256_transform_optimized(state, padded_block);
    
    // Output result
    for (int i = 0; i < 8; i++) {
        ((uint32_t*)result)[i] = bswap32(state[i]);
    }
    
    return true;
}

// Optimized double hash
bool sha256_double_hash_optimized(const uint32_t* midstate, const uint8_t* data, uint8_t* result) {
    return sha256_double_hash(midstate, data, result);
}

// Byte order conversion
void byte_reverse_words(uint32_t* out, const uint32_t* in, size_t count) {
    for (size_t i = 0; i < count; i++) {
        out[i] = bswap32(in[i]);
    }
}

// Check target difficulty
bool check_target(const uint8_t* hash, const uint8_t* target) {
    for (int i = 31; i >= 0; i--) {
        if (hash[i] > target[i]) return false;
        if (hash[i] < target[i]) return true;
    }
    return true;
}

// Calculate difficulty value
double calculate_difficulty(const uint8_t* hash) {
    // Simplified difficulty calculation - count leading zeros
    int leading_zeros = 0;
    for (int i = 31; i >= 0; i--) {
        if (hash[i] == 0) {
            leading_zeros += 8;
        } else {
            uint8_t byte = hash[i];
            while ((byte & 0x80) == 0 && byte != 0) {
                leading_zeros++;
                byte <<= 1;
            }
            break;
        }
    }
    
    // Return difficulty value based on leading zeros count
    return static_cast<double>(1ULL << std::min(leading_zeros, 63));
}

// SHA256 performance test
void benchmark_sha256() {
    const int iterations = 1000000;
    uint8_t test_data[64];
    uint32_t state[8];
    
    // Initialize test data
    for (int i = 0; i < 64; i++) {
        test_data[i] = static_cast<uint8_t>(i);
    }
    
    auto start = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < iterations; i++) {
        sha256_transform_optimized(state, test_data);
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    
    std::cout << "SHA256 Benchmark: " << iterations << " iterations in " 
              << duration.count() << " microseconds" << std::endl;
    std::cout << "Rate: " << (iterations * 1000000.0 / duration.count()) 
              << " hashes/second" << std::endl;
}

// Batch processing performance test
void benchmark_batch_processing() {
    const uint32_t batch_size = 10000;
    SHA256Context context;
    uint8_t block_template[80];
    BatchResult results[1000];
    uint32_t found_count;
    uint8_t target[32];
    
    // Initialize test data
    memset(block_template, 0, 80);
    memset(target, 0xFF, 32);
    target[31] = 0x0F; // Simple target
    
    auto start = std::chrono::high_resolution_clock::now();
    
    sha256_batch_ultra_fast(&context, block_template, 0, batch_size, 
                           results, &found_count, target);
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    
    std::cout << "Batch Processing Benchmark: " << batch_size << " hashes in " 
              << duration.count() << " microseconds" << std::endl;
    std::cout << "Rate: " << (batch_size * 1000000.0 / duration.count()) 
              << " hashes/second" << std::endl;
    std::cout << "Found " << found_count << " valid hashes" << std::endl;
}

} // namespace SHA256Core 