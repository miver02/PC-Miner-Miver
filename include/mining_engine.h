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

// Mining statistics
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

// Mining configuration
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
        // Auto detect CPU core count
        thread_count = std::thread::hardware_concurrency();
        if (thread_count == 0) thread_count = 4;
    }
};

// Mining worker thread state
enum class WorkerState {
    IDLE,
    WORKING,
    PAUSED,
    STOPPED
};

// Worker thread information
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

// Mining engine main class
class MiningEngine {
public:
    // Callback function types
    using StatsCallback = std::function<void(const MiningStats&)>;
    using ShareFoundCallback = std::function<void(const StratumShare&, bool)>;
    using ErrorCallback = std::function<void(const std::string&)>;
    using StatusCallback = std::function<void(const std::string&)>;
    
    // Constructor
    MiningEngine();
    ~MiningEngine();
    
    // Configuration management
    void set_config(const MiningConfig& config);
    const MiningConfig& get_config() const { return config_; }
    
    // Mining control
    bool start_mining();
    void stop_mining();
    void pause_mining();
    void resume_mining();
    bool is_mining() const;
    
    // Connection management
    bool connect_to_pool();
    void disconnect_from_pool();
    bool is_connected() const;
    
    // Statistics
    const MiningStats& get_stats() const { return stats_; }
    void reset_stats();
    
    // Callback settings
    void set_stats_callback(StatsCallback callback);
    void set_share_found_callback(ShareFoundCallback callback);
    void set_error_callback(ErrorCallback callback);
    void set_status_callback(StatusCallback callback);
    
    // Worker thread information
    std::vector<WorkerInfo*> get_worker_info() const;
    
    // Performance testing
    double benchmark_performance(int duration_seconds = 30);
    
private:
    // Configuration and state
    MiningConfig config_;
    std::atomic<bool> mining_active_;
    std::atomic<bool> mining_paused_;
    std::atomic<bool> should_stop_;
    
    // Network client
    std::unique_ptr<StratumClient> stratum_client_;
    
    // Statistics
    MiningStats stats_;
    mutable std::mutex stats_mutex_;
    
    // Worker thread management
    std::vector<std::unique_ptr<WorkerInfo>> workers_;
    mutable std::mutex workers_mutex_;
    
    // Current mining job
    StratumJob current_job_;
    mutable std::mutex job_mutex_;
    std::condition_variable job_condition_;
    bool job_available_;
    
    // Callback functions
    StatsCallback stats_callback_;
    ShareFoundCallback share_found_callback_;
    ErrorCallback error_callback_;
    StatusCallback status_callback_;
    mutable std::mutex callback_mutex_;
    
    // Statistics update thread
    std::thread stats_thread_;
    std::atomic<bool> stats_thread_running_;
    
    // Internal methods
    void initialize_workers();
    void cleanup_workers();
    void worker_thread_function(WorkerInfo* worker);
    
    // Stratum event handling
    void on_job_received(const StratumJob& job);
    void on_difficulty_changed(double difficulty);
    void on_stratum_error(const std::string& error);
    void on_connection_status(bool connected);
    
    // Mining core logic
    bool process_mining_work(WorkerInfo* worker, const StratumJob& job);
    bool check_and_submit_share(const StratumJob& job, uint32_t nonce, 
                               const uint8_t* hash, WorkerInfo* worker);
    
    // Statistics update
    void stats_update_loop();
    void update_hashrate();
    void trigger_stats_callback();
    
    // Utility methods
    void distribute_nonce_ranges();
    uint32_t get_next_nonce_range(int worker_id, uint32_t range_size);
    
    // Error handling
    void handle_error(const std::string& error);
    void handle_status_change(const std::string& status);
    
    // Auto reconnect
    void auto_reconnect_loop();
    std::thread reconnect_thread_;
    std::atomic<bool> reconnect_thread_running_;
    
    // Disable copy
    MiningEngine(const MiningEngine&) = delete;
    MiningEngine& operator=(const MiningEngine&) = delete;
};

// Utility functions
namespace MiningUtils {
    // CPU feature detection
    bool has_avx2_support();
    bool has_sse2_support();
    int get_optimal_thread_count();
    
    // Performance optimization
    void set_thread_affinity(std::thread& thread, int cpu_id);
    void set_thread_priority(std::thread& thread, int priority);
    
    // Configuration file
    bool load_config_from_file(const std::string& filename, MiningConfig& config);
    bool save_config_to_file(const std::string& filename, const MiningConfig& config);
    
    // Formatting tools
    std::string format_hashrate(double hashrate);
    std::string format_duration(std::chrono::seconds duration);
    std::string format_timestamp(std::chrono::system_clock::time_point time);
    
    // Difficulty calculation
    double calculate_share_difficulty(const uint8_t* hash);
    bool meets_target_difficulty(const uint8_t* hash, double target_difficulty);
} 