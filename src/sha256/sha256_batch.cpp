#include "sha256_core.h"
#include <algorithm>
#include <cstring>
#include <thread>
#include <vector>

namespace SHA256Core {

// 批处理配置
constexpr uint32_t DEFAULT_BATCH_SIZE = 1024;
constexpr uint32_t MAX_BATCH_SIZE = 4096;
constexpr uint32_t MIN_BATCH_SIZE = 256;

// 批处理SHA256处理
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
    
    // 限制批处理大小
    batch_size = std::min(batch_size, MAX_BATCH_SIZE);
    batch_size = std::max(batch_size, MIN_BATCH_SIZE);
    
    uint32_t midstate[8];
    uint8_t work_block[80] ALIGN(64);
    uint8_t hash_result[32] ALIGN(32);
    
    // 复制区块模板
    memcpy(work_block, block_template, 80);
    
    // 计算中间状态
    sha256_midstate_optimized(midstate, work_block);
    
    // 批处理循环
    for (uint32_t i = 0; i < batch_size; i++) {
        uint32_t nonce = start_nonce + i;
        
        // 设置nonce (通常在位置76-79)
        *((uint32_t*)(work_block + 76)) = bswap32(nonce);
        
        // 计算双重SHA256
        if (sha256_double_hash_optimized(midstate, work_block + 64, hash_result)) {
            // 检查是否满足目标难度
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

// 超高性能批处理版本
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
    
    // 优化的批处理大小
    batch_size = std::min(batch_size, MAX_BATCH_SIZE);
    batch_size = std::max(batch_size, MIN_BATCH_SIZE);
    
    // 对齐的工作缓冲区
    uint32_t midstate[8] ALIGN(32);
    uint8_t work_blocks[4][80] ALIGN(64);  // 4路并行
    uint8_t hash_results[4][32] ALIGN(32);
    
    // 复制区块模板到所有工作块
    for (int j = 0; j < 4; j++) {
        memcpy(work_blocks[j], block_template, 80);
    }
    
    // 计算中间状态
    sha256_midstate_optimized(midstate, work_blocks[0]);
    
    // 4路并行批处理循环
    for (uint32_t i = 0; i < batch_size; i += 4) {
        // 设置4个不同的nonce
        for (int j = 0; j < 4 && (i + j) < batch_size; j++) {
            uint32_t nonce = start_nonce + i + j;
            *((uint32_t*)(work_blocks[j] + 76)) = bswap32(nonce);
        }
        
        // 并行计算4个哈希
        for (int j = 0; j < 4 && (i + j) < batch_size; j++) {
            if (sha256_double_hash_optimized(midstate, work_blocks[j] + 64, hash_results[j])) {
                // 检查是否满足目标难度
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
// AVX2优化版本
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
    
    // AVX2可以并行处理8个32位整数
    constexpr int AVX2_LANES = 8;
    
    batch_size = std::min(batch_size, MAX_BATCH_SIZE);
    batch_size = std::max(batch_size, MIN_BATCH_SIZE);
    
    // 对齐的工作缓冲区
    uint32_t midstate[8] ALIGN(32);
    uint8_t work_blocks[AVX2_LANES][80] ALIGN(64);
    uint8_t hash_results[AVX2_LANES][32] ALIGN(32);
    
    // 复制区块模板
    for (int j = 0; j < AVX2_LANES; j++) {
        memcpy(work_blocks[j], block_template, 80);
    }
    
    // 计算中间状态
    sha256_midstate_optimized(midstate, work_blocks[0]);
    
    // AVX2并行批处理循环
    for (uint32_t i = 0; i < batch_size; i += AVX2_LANES) {
        // 设置8个不同的nonce
        __m256i nonces = _mm256_set_epi32(
            start_nonce + i + 7, start_nonce + i + 6,
            start_nonce + i + 5, start_nonce + i + 4,
            start_nonce + i + 3, start_nonce + i + 2,
            start_nonce + i + 1, start_nonce + i + 0
        );
        
        // 字节序转换
        __m256i swapped_nonces = _mm256_shuffle_epi8(nonces, 
            _mm256_set_epi8(12,13,14,15, 8,9,10,11, 4,5,6,7, 0,1,2,3,
                           12,13,14,15, 8,9,10,11, 4,5,6,7, 0,1,2,3));
        
        // 存储nonce到工作块
        uint32_t nonce_array[AVX2_LANES];
        _mm256_store_si256((__m256i*)nonce_array, swapped_nonces);
        
        for (int j = 0; j < AVX2_LANES && (i + j) < batch_size; j++) {
            *((uint32_t*)(work_blocks[j] + 76)) = nonce_array[j];
        }
        
        // 计算哈希并检查结果
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

// 多线程批处理
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
        
        // 使用超高性能批处理
        if (sha256_batch_ultra_fast(
            data->context,
            data->block_template,
            data->start_nonce,
            data->batch_size,
            local_results,
            &local_found,
            data->target
        )) {
            // 原子性地更新结果
            uint32_t old_count = data->found_count->fetch_add(local_found);
            for (uint32_t i = 0; i < local_found && (old_count + i) < MAX_BATCH_SIZE; i++) {
                data->results[old_count + i] = local_results[i];
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
        int num_threads = 0
    ) {
        if (num_threads <= 0) {
            num_threads = std::thread::hardware_concurrency();
        }
        
        *found_count = 0;
        std::atomic<uint32_t> atomic_found_count(0);
        std::atomic<bool> stop_flag(false);
        
        uint32_t batch_per_thread = total_batch_size / num_threads;
        std::vector<std::thread> threads;
        std::vector<WorkerData> worker_data(num_threads);
        
        // 启动工作线程
        for (int i = 0; i < num_threads; i++) {
            worker_data[i] = {
                context,
                block_template,
                start_nonce + i * batch_per_thread,
                (i == num_threads - 1) ? (total_batch_size - i * batch_per_thread) : batch_per_thread,
                results,
                &atomic_found_count,
                target,
                &stop_flag
            };
            
            threads.emplace_back(worker_thread, &worker_data[i]);
        }
        
        // 等待所有线程完成
        for (auto& thread : threads) {
            thread.join();
        }
        
        *found_count = atomic_found_count.load();
        return true;
    }
};

// 公共接口：多线程批处理
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

// 自适应批处理大小
uint32_t calculate_optimal_batch_size(double current_hashrate, int num_threads) {
    // 基于当前算力和线程数计算最优批处理大小
    uint32_t base_size = DEFAULT_BATCH_SIZE;
    
    if (current_hashrate > 1000000) {  // > 1MH/s
        base_size = MAX_BATCH_SIZE;
    } else if (current_hashrate > 500000) {  // > 500kH/s
        base_size = 2048;
    } else if (current_hashrate > 100000) {  // > 100kH/s
        base_size = 1024;
    } else {
        base_size = MIN_BATCH_SIZE;
    }
    
    // 根据线程数调整
    base_size = (base_size / num_threads) * num_threads;
    
    return std::max(MIN_BATCH_SIZE, std::min(MAX_BATCH_SIZE, base_size));
}

} // namespace SHA256Core 