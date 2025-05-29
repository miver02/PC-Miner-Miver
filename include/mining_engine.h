#pragma once

#include "sha256_core.h"
#include "stratum_client.h"
#include <memory>
#include <atomic>
#include <thread>
#include <vector>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <functional>

// 挖矿统计信息
struct MiningStats {
    std::atomic<uint64_t> total_hashes;
    std::atomic<uint64_t> valid_shares;
    std::atomic<uint64_t> invalid_shares;
    std::atomic<double> current_hashrate;
    std::atomic<double> average_hashrate;
    std::chrono::steady_clock::time_point start_time;
    std::chrono::steady_clock::time_point last_update;
    
    MiningStats() : total_hashes(0), valid_shares(0), invalid_shares(0),
                   current_hashrate(0.0), average_hashrate(0.0) {
        start_time = std::chrono::steady_clock::now();
        last_update = start_time;
    }
    
    void reset() {
        total_hashes = 0;
        valid_shares = 0;
        invalid_shares = 0;
        current_hashrate = 0.0;
        average_hashrate = 0.0;
        start_time = std::chrono::steady_clock::now();
        last_update = start_time;
    }
};

// 挖矿配置
struct MiningConfig {
    std::string pool_host;
    int pool_port;
    std::string username;
    std::string password;
    int thread_count;
    uint32_t batch_size;
    bool enable_avx2;
    bool enable_optimization;
    double target_difficulty;
    int stats_interval_seconds;
    bool auto_reconnect;
    int reconnect_delay_seconds;
    
    MiningConfig() : pool_port(3333), password("x"), thread_count(0),
                    batch_size(1000000), enable_avx2(true), enable_optimization(true),
                    target_difficulty(1.0), stats_interval_seconds(10),
                    auto_reconnect(true), reconnect_delay_seconds(30) {
        // 自动检测CPU核心数
        thread_count = std::thread::hardware_concurrency();
        if (thread_count == 0) thread_count = 4;
    }
};

// 挖矿工作线程状态
enum class WorkerState {
    IDLE,
    WORKING,
    PAUSED,
    STOPPED
};

// 工作线程信息
struct WorkerInfo {
    int worker_id;
    std::atomic<WorkerState> state;
    std::atomic<uint64_t> hashes_done;
    std::atomic<double> hashrate;
    std::thread worker_thread;
    uint32_t start_nonce;
    uint32_t end_nonce;
    
    WorkerInfo(int id) : worker_id(id), state(WorkerState::IDLE),
                        hashes_done(0), hashrate(0.0),
                        start_nonce(0), end_nonce(0) {}
};

// 挖矿引擎主类
class MiningEngine {
public:
    // 回调函数类型
    using StatsCallback = std::function<void(const MiningStats&)>;
    using ShareFoundCallback = std::function<void(const StratumShare&, bool)>;
    using ErrorCallback = std::function<void(const std::string&)>;
    using StatusCallback = std::function<void(const std::string&)>;
    
    // 构造函数
    MiningEngine();
    ~MiningEngine();
    
    // 配置管理
    void set_config(const MiningConfig& config);
    const MiningConfig& get_config() const { return config_; }
    
    // 挖矿控制
    bool start_mining();
    void stop_mining();
    void pause_mining();
    void resume_mining();
    bool is_mining() const;
    
    // 连接管理
    bool connect_to_pool();
    void disconnect_from_pool();
    bool is_connected() const;
    
    // 统计信息
    const MiningStats& get_stats() const { return stats_; }
    void reset_stats();
    
    // 回调设置
    void set_stats_callback(StatsCallback callback);
    void set_share_found_callback(ShareFoundCallback callback);
    void set_error_callback(ErrorCallback callback);
    void set_status_callback(StatusCallback callback);
    
    // 工作线程信息
    std::vector<WorkerInfo*> get_worker_info() const;
    
    // 性能测试
    double benchmark_performance(int duration_seconds = 30);
    
private:
    // 配置和状态
    MiningConfig config_;
    std::atomic<bool> mining_active_;
    std::atomic<bool> mining_paused_;
    std::atomic<bool> should_stop_;
    
    // 网络客户端
    std::unique_ptr<StratumClient> stratum_client_;
    
    // 统计信息
    MiningStats stats_;
    std::mutex stats_mutex_;
    
    // 工作线程管理
    std::vector<std::unique_ptr<WorkerInfo>> workers_;
    std::mutex workers_mutex_;
    
    // 当前挖矿任务
    StratumJob current_job_;
    std::mutex job_mutex_;
    std::condition_variable job_condition_;
    bool job_available_;
    
    // 回调函数
    StatsCallback stats_callback_;
    ShareFoundCallback share_found_callback_;
    ErrorCallback error_callback_;
    StatusCallback status_callback_;
    std::mutex callback_mutex_;
    
    // 统计更新线程
    std::thread stats_thread_;
    std::atomic<bool> stats_thread_running_;
    
    // 内部方法
    void initialize_workers();
    void cleanup_workers();
    void worker_thread_function(WorkerInfo* worker);
    
    // Stratum事件处理
    void on_job_received(const StratumJob& job);
    void on_difficulty_changed(double difficulty);
    void on_stratum_error(const std::string& error);
    void on_connection_status(bool connected);
    
    // 挖矿核心逻辑
    bool process_mining_work(WorkerInfo* worker, const StratumJob& job);
    bool check_and_submit_share(const StratumJob& job, uint32_t nonce, 
                               const uint8_t* hash, WorkerInfo* worker);
    
    // 统计更新
    void stats_update_loop();
    void update_hashrate();
    void trigger_stats_callback();
    
    // 工具方法
    void distribute_nonce_ranges();
    uint32_t get_next_nonce_range(int worker_id, uint32_t range_size);
    
    // 错误处理
    void handle_error(const std::string& error);
    void handle_status_change(const std::string& status);
    
    // 自动重连
    void auto_reconnect_loop();
    std::thread reconnect_thread_;
    std::atomic<bool> reconnect_thread_running_;
    
    // 禁用拷贝
    MiningEngine(const MiningEngine&) = delete;
    MiningEngine& operator=(const MiningEngine&) = delete;
};

// 工具函数
namespace MiningUtils {
    // CPU特性检测
    bool has_avx2_support();
    bool has_sse2_support();
    int get_optimal_thread_count();
    
    // 性能优化
    void set_thread_affinity(std::thread& thread, int cpu_id);
    void set_thread_priority(std::thread& thread, int priority);
    
    // 配置文件
    bool load_config_from_file(const std::string& filename, MiningConfig& config);
    bool save_config_to_file(const std::string& filename, const MiningConfig& config);
    
    // 格式化工具
    std::string format_hashrate(double hashrate);
    std::string format_duration(std::chrono::seconds duration);
    std::string format_timestamp(std::chrono::system_clock::time_point time);
    
    // 难度计算
    double calculate_share_difficulty(const uint8_t* hash);
    bool meets_target_difficulty(const uint8_t* hash, double target_difficulty);
} 