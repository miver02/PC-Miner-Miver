#include "sha256_core.h"
#include <algorithm>
#include <cstring>
#include <thread>
#include <vector>
#include <atomic>

namespace SHA256Core {

// Batch processing configuration
constexpr uint32_t DEFAULT_BATCH_SIZE = 1024;
constexpr uint32_t MAX_BATCH_SIZE = 4096;
constexpr uint32_t MIN_BATCH_SIZE = 256;

// Batch SHA256 processing
bool sha256_batch_process(
    const SHA256Context* context,
    const uint8_t* block_template,
    uint32_t start_nonce,
    uint32_t batch_size,
    BatchResult* results,
    uint32_t* found_count,
    const uint8_t* target
) {
    *found_count = 0;
    
    // Limit batch size
    batch_size = std::min(batch_size, MAX_BATCH_SIZE);
    batch_size = std::max(batch_size, MIN_BATCH_SIZE);
    
    ALIGN(32) uint32_t midstate[8];
    ALIGN(64) uint8_t work_block[80];
    ALIGN(32) uint8_t hash_result[32];
    
    // Copy block template
    memcpy(work_block, block_template, 80);
    
    // Calculate intermediate state
    sha256_midstate_optimized(midstate, work_block);
    
    // Batch processing loop
    for (uint32_t i = 0; i < batch_size; i++) {
        uint32_t nonce = start_nonce + i;
        
        // Set nonce (usually at position 76-79)
        *((uint32_t*)(work_block + 76)) = bswap32(nonce);
        
        // Calculate double SHA256
        if (sha256_double_hash_optimized(midstate, work_block + 64, hash_result)) {
            // Check if target difficulty is met
            if (check_target(hash_result, target)) {
                if (*found_count < batch_size) {
                    results[*found_count].nonce = nonce;
                    memcpy(results[*found_count].hash, hash_result, 32);
                    results[*found_count].difficulty = calculate_difficulty(hash_result);
                    results[*found_count].is_valid = true;
                    (*found_count)++;
                }
            }
        }
    }
    
    return true;
}

// Ultra high performance batch processing version
bool sha256_batch_ultra_fast(
    const SHA256Context* context,
    const uint8_t* block_template,
    uint32_t start_nonce,
    uint32_t batch_size,
    BatchResult* results,
    uint32_t* found_count,
    const uint8_t* target
) {
    *found_count = 0;
    
    // Optimized batch size
    batch_size = std::min(batch_size, MAX_BATCH_SIZE);
    batch_size = std::max(batch_size, MIN_BATCH_SIZE);
    
    // Aligned work buffers
    ALIGN(32) uint32_t midstate[8];
    ALIGN(64) uint8_t work_blocks[4][80];  // 4-way parallel
    ALIGN(32) uint8_t hash_results[4][32];
    
    // Copy block template to all work blocks
    for (int j = 0; j < 4; j++) {
        memcpy(work_blocks[j], block_template, 80);
    }
    
    // Calculate intermediate state
    sha256_midstate_optimized(midstate, work_blocks[0]);
    
    // 4-way parallel batch processing loop
    for (uint32_t i = 0; i < batch_size; i += 4) {
        // Set 4 different nonces
        for (int j = 0; j < 4 && (i + j) < batch_size; j++) {
            uint32_t nonce = start_nonce + i + j;
            *((uint32_t*)(work_blocks[j] + 76)) = bswap32(nonce);
        }
        
        // Parallel compute 4 hashes
        for (int j = 0; j < 4 && (i + j) < batch_size; j++) {
            if (sha256_double_hash_optimized(midstate, work_blocks[j] + 64, hash_results[j])) {
                // Check if target difficulty is met
                if (check_target(hash_results[j], target)) {
                    if (*found_count < batch_size) {
                        results[*found_count].nonce = start_nonce + i + j;
                        memcpy(results[*found_count].hash, hash_results[j], 32);
                        results[*found_count].difficulty = calculate_difficulty(hash_results[j]);
                        results[*found_count].is_valid = true;
                        (*found_count)++;
                    }
                }
            }
        }
    }
    
    return true;
}

#ifdef __AVX2__
// AVX2 optimized version
bool sha256_batch_avx2(
    const SHA256Context* context,
    const uint8_t* block_template,
    uint32_t start_nonce,
    uint32_t batch_size,
    BatchResult* results,
    uint32_t* found_count,
    const uint8_t* target
) {
    *found_count = 0;
    
    // AVX2 can process 8 32-bit integers in parallel
    constexpr int AVX2_LANES = 8;
    
    batch_size = std::min(batch_size, MAX_BATCH_SIZE);
    batch_size = std::max(batch_size, MIN_BATCH_SIZE);
    
    // Aligned work buffers
    ALIGN(32) uint32_t midstate[8];
    ALIGN(64) uint8_t work_blocks[AVX2_LANES][80];
    ALIGN(32) uint8_t hash_results[AVX2_LANES][32];
    
    // Copy block template
    for (int j = 0; j < AVX2_LANES; j++) {
        memcpy(work_blocks[j], block_template, 80);
    }
    
    // Calculate intermediate state
    sha256_midstate_optimized(midstate, work_blocks[0]);
    
    // AVX2 parallel batch processing loop
    for (uint32_t i = 0; i < batch_size; i += AVX2_LANES) {
        // Set 8 different nonces
        __m256i nonces = _mm256_set_epi32(
            start_nonce + i + 7, start_nonce + i + 6,
            start_nonce + i + 5, start_nonce + i + 4,
            start_nonce + i + 3, start_nonce + i + 2,
            start_nonce + i + 1, start_nonce + i + 0
        );
        
        // Byte order conversion
        __m256i swapped_nonces = _mm256_shuffle_epi8(nonces, 
            _mm256_set_epi8(12,13,14,15, 8,9,10,11, 4,5,6,7, 0,1,2,3,
                           12,13,14,15, 8,9,10,11, 4,5,6,7, 0,1,2,3));
        
        // Store nonce to work blocks
        uint32_t nonce_array[AVX2_LANES];
        _mm256_store_si256((__m256i*)nonce_array, swapped_nonces);
        
        for (int j = 0; j < AVX2_LANES && (i + j) < batch_size; j++) {
            *((uint32_t*)(work_blocks[j] + 76)) = nonce_array[j];
        }
        
        // Calculate hash and check results
        for (int j = 0; j < AVX2_LANES && (i + j) < batch_size; j++) {
            if (sha256_double_hash_optimized(midstate, work_blocks[j] + 64, hash_results[j])) {
                if (check_target(hash_results[j], target)) {
                    if (*found_count < batch_size) {
                        results[*found_count].nonce = start_nonce + i + j;
                        memcpy(results[*found_count].hash, hash_results[j], 32);
                        results[*found_count].difficulty = calculate_difficulty(hash_results[j]);
                        results[*found_count].is_valid = true;
                        (*found_count)++;
                    }
                }
            }
        }
    }
    
    return true;
}
#endif

// Multi-threaded batch processing
class BatchProcessor {
private:
    struct WorkerData {
        const SHA256Context* context;
        const uint8_t* block_template;
        uint32_t start_nonce;
        uint32_t batch_size;
        BatchResult* results;
        uint32_t* found_count;
        const uint8_t* target;
        std::atomic<bool>* stop_flag;
    };

    static void worker_thread(WorkerData* data) {
        uint32_t local_found = 0;
        BatchResult local_results[MAX_BATCH_SIZE];
        
        if (!data->stop_flag->load()) {
            sha256_batch_ultra_fast(
                data->context,
                data->block_template,
                data->start_nonce,
                data->batch_size,
                local_results,
                &local_found,
                data->target
            );
            
            // Copy results to main result array
            for (uint32_t i = 0; i < local_found; i++) {
                if (*data->found_count < MAX_BATCH_SIZE) {
                    data->results[*data->found_count] = local_results[i];
                    (*data->found_count)++;
                }
            }
        }
    }

public:
    static bool process_multithreaded(
        const SHA256Context* context,
        const uint8_t* block_template,
        uint32_t start_nonce,
        uint32_t total_batch_size,
        BatchResult* results,
        uint32_t* found_count,
        const uint8_t* target,
        int num_threads
    ) {
        *found_count = 0;
        
        if (num_threads <= 0) {
            num_threads = std::thread::hardware_concurrency();
        }
        
        uint32_t batch_per_thread = total_batch_size / num_threads;
        std::vector<std::thread> threads;
        std::vector<WorkerData> worker_data(num_threads);
        std::atomic<bool> stop_flag(false);
        
        // Start worker threads
        for (int i = 0; i < num_threads; i++) {
            worker_data[i] = {
                context,
                block_template,
                start_nonce + i * batch_per_thread,
                batch_per_thread,
                results,
                found_count,
                target,
                &stop_flag
            };
            
            threads.emplace_back(worker_thread, &worker_data[i]);
        }
        
        // Wait for all threads to complete
        for (auto& thread : threads) {
            thread.join();
        }
        
        return true;
    }
};

// Multi-threaded batch processing interface
bool sha256_batch_multithreaded(
    const SHA256Context* context,
    const uint8_t* block_template,
    uint32_t start_nonce,
    uint32_t batch_size,
    BatchResult* results,
    uint32_t* found_count,
    const uint8_t* target,
    int num_threads
) {
    return BatchProcessor::process_multithreaded(
        context, block_template, start_nonce, batch_size,
        results, found_count, target, num_threads
    );
}

// Calculate optimal batch processing size
uint32_t calculate_optimal_batch_size(double current_hashrate, int num_threads) {
    // Calculate optimal batch processing size based on hashrate and thread count
    uint32_t base_size = static_cast<uint32_t>(current_hashrate / 1000.0);
    base_size = std::max(base_size, MIN_BATCH_SIZE);
    base_size = std::min(base_size, MAX_BATCH_SIZE);
    
    // Adjust based on thread count
    base_size = (base_size / num_threads) * num_threads;
    
    return base_size;
}

} // namespace SHA256Core 