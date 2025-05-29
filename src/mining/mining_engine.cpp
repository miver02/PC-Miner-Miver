#include "mining_engine.h"
#include <iostream>
#include <iomanip>
#include <sstream>
#include <fstream>
#include <algorithm>
#include <random>

#ifdef _WIN32
    #include <windows.h>
    #include <intrin.h>
#else
    #include <cpuid.h>
    #include <pthread.h>
    #include <sched.h>
#endif

// MiningEngine实现
MiningEngine::MiningEngine() 
    : mining_active_(false), mining_paused_(false), should_stop_(false),
      job_available_(false), stats_thread_running_(false),
      reconnect_thread_running_(false) {
    
    stratum_client_ = std::make_unique<StratumClient>();
    
    // 设置Stratum回调
    stratum_client_->set_job_callback(
        [this](const StratumJob& job) { on_job_received(job); });
    stratum_client_->set_difficulty_callback(
        [this](double difficulty) { on_difficulty_changed(difficulty); });
    stratum_client_->set_error_callback(
        [this](const std::string& error) { on_stratum_error(error); });
    stratum_client_->set_connect_callback(
        [this](bool connected) { on_connection_status(connected); });
}

MiningEngine::~MiningEngine() {
    stop_mining();
    disconnect_from_pool();
    cleanup_workers();
}

void MiningEngine::set_config(const MiningConfig& config) {
    config_ = config;
    
    // 验证配置
    if (config_.thread_count <= 0) {
        config_.thread_count = std::thread::hardware_concurrency();
        if (config_.thread_count == 0) config_.thread_count = 4;
    }
    
    if (config_.batch_size < 1000) {
        config_.batch_size = 1000;
    } else if (config_.batch_size > 10000000) {
        config_.batch_size = 10000000;
    }
}

bool MiningEngine::start_mining() {
    if (mining_active_) {
        return true;
    }
    
    if (!stratum_client_->is_connected()) {
        handle_error("未连接到矿池，无法开始挖矿");
        return false;
    }
    
    should_stop_ = false;
    mining_active_ = true;
    mining_paused_ = false;
    
    // 重置统计信息
    stats_.reset();
    
    // 初始化工作线程
    initialize_workers();
    
    // 启动统计更新线程
    if (!stats_thread_running_) {
        stats_thread_running_ = true;
        stats_thread_ = std::thread(&MiningEngine::stats_update_loop, this);
    }
    
    handle_status_change("挖矿已开始");
    return true;
}

void MiningEngine::stop_mining() {
    if (!mining_active_) {
        return;
    }
    
    should_stop_ = true;
    mining_active_ = false;
    mining_paused_ = false;
    
    // 通知所有等待的线程
    job_condition_.notify_all();
    
    // 停止统计线程
    if (stats_thread_running_) {
        stats_thread_running_ = false;
        if (stats_thread_.joinable()) {
            stats_thread_.join();
        }
    }
    
    // 清理工作线程
    cleanup_workers();
    
    handle_status_change("挖矿已停止");
}

void MiningEngine::pause_mining() {
    if (!mining_active_ || mining_paused_) {
        return;
    }
    
    mining_paused_ = true;
    handle_status_change("挖矿已暂停");
}

void MiningEngine::resume_mining() {
    if (!mining_active_ || !mining_paused_) {
        return;
    }
    
    mining_paused_ = false;
    job_condition_.notify_all();
    handle_status_change("挖矿已恢复");
}

bool MiningEngine::is_mining() const {
    return mining_active_ && !mining_paused_;
}

bool MiningEngine::connect_to_pool() {
    if (config_.pool_host.empty()) {
        handle_error("矿池地址未配置");
        return false;
    }
    
    handle_status_change("正在连接到矿池: " + config_.pool_host + ":" + std::to_string(config_.pool_port));
    
    if (!stratum_client_->connect(config_.pool_host, config_.pool_port)) {
        handle_error("连接矿池失败");
        return false;
    }
    
    // 启动消息处理循环
    stratum_client_->start_message_loop();
    
    // 订阅挖矿服务
    if (!stratum_client_->subscribe()) {
        handle_error("订阅挖矿服务失败");
        return false;
    }
    
    // 认证用户
    if (!stratum_client_->authorize(config_.username, config_.password)) {
        handle_error("用户认证失败");
        return false;
    }
    
    handle_status_change("已连接到矿池");
    
    // 启动自动重连线程
    if (config_.auto_reconnect && !reconnect_thread_running_) {
        reconnect_thread_running_ = true;
        reconnect_thread_ = std::thread(&MiningEngine::auto_reconnect_loop, this);
    }
    
    return true;
}

void MiningEngine::disconnect_from_pool() {
    if (reconnect_thread_running_) {
        reconnect_thread_running_ = false;
        if (reconnect_thread_.joinable()) {
            reconnect_thread_.join();
        }
    }
    
    stratum_client_->stop_message_loop();
    stratum_client_->disconnect();
    handle_status_change("已断开矿池连接");
}

bool MiningEngine::is_connected() const {
    return stratum_client_->is_connected();
}

void MiningEngine::reset_stats() {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    stats_.reset();
}

void MiningEngine::set_stats_callback(StatsCallback callback) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    stats_callback_ = callback;
}

void MiningEngine::set_share_found_callback(ShareFoundCallback callback) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    share_found_callback_ = callback;
}

void MiningEngine::set_error_callback(ErrorCallback callback) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    error_callback_ = callback;
}

void MiningEngine::set_status_callback(StatusCallback callback) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    status_callback_ = callback;
}

std::vector<WorkerInfo*> MiningEngine::get_worker_info() const {
    std::lock_guard<std::mutex> lock(workers_mutex_);
    std::vector<WorkerInfo*> result;
    for (const auto& worker : workers_) {
        result.push_back(worker.get());
    }
    return result;
}

double MiningEngine::benchmark_performance(int duration_seconds) {
    handle_status_change("开始性能测试...");
    
    // 创建测试任务
    StratumJob test_job;
    test_job.job_id = "benchmark";
    test_job.prev_block_hash = "0000000000000000000000000000000000000000000000000000000000000000";
    test_job.version = "20000000";
    test_job.nbits = "1d00ffff";
    test_job.ntime = "5f2a7b5f";
    
    // 创建SHA256上下文
    SHA256Context ctx;
    uint64_t total_hashes = 0;
    auto start_time = std::chrono::steady_clock::now();
    auto end_time = start_time + std::chrono::seconds(duration_seconds);
    
    // 运行基准测试
    while (std::chrono::steady_clock::now() < end_time) {
        uint8_t hash[32];
        uint32_t nonce = static_cast<uint32_t>(total_hashes);
        
        if (config_.enable_optimization) {
            SHA256Core::sha256_transform_optimized(ctx.buffer, ctx.state);
        } else {
            SHA256Core::sha256_transform(ctx.buffer, ctx.state);
        }
        
        total_hashes++;
        
        // 每10万次哈希检查一次时间
        if (total_hashes % 100000 == 0) {
            if (std::chrono::steady_clock::now() >= end_time) {
                break;
            }
        }
    }
    
    auto actual_duration = std::chrono::steady_clock::now() - start_time;
    double seconds = std::chrono::duration<double>(actual_duration).count();
    double hashrate = total_hashes / seconds;
    
    handle_status_change("性能测试完成: " + MiningUtils::format_hashrate(hashrate));
    return hashrate;
}

void MiningEngine::initialize_workers() {
    std::lock_guard<std::mutex> lock(workers_mutex_);
    
    // 清理现有工作线程
    cleanup_workers();
    
    // 创建新的工作线程
    workers_.reserve(config_.thread_count);
    for (int i = 0; i < config_.thread_count; ++i) {
        auto worker = std::make_unique<WorkerInfo>(i);
        worker->state = WorkerState::IDLE;
        workers_.push_back(std::move(worker));
    }
    
    // 分配nonce范围
    distribute_nonce_ranges();
    
    // 启动工作线程
    for (auto& worker : workers_) {
        worker->worker_thread = std::thread(&MiningEngine::worker_thread_function, this, worker.get());
        
        // 设置线程亲和性（如果支持）
        if (config_.enable_optimization) {
            MiningUtils::set_thread_affinity(worker->worker_thread, worker->worker_id);
        }
    }
}

void MiningEngine::cleanup_workers() {
    std::lock_guard<std::mutex> lock(workers_mutex_);
    
    // 等待所有工作线程结束
    for (auto& worker : workers_) {
        worker->state = WorkerState::STOPPED;
        if (worker->worker_thread.joinable()) {
            worker->worker_thread.join();
        }
    }
    
    workers_.clear();
}

void MiningEngine::worker_thread_function(WorkerInfo* worker) {
    worker->state = WorkerState::IDLE;
    
    while (!should_stop_ && worker->state != WorkerState::STOPPED) {
        // 等待挖矿任务
        std::unique_lock<std::mutex> lock(job_mutex_);
        job_condition_.wait(lock, [this] { 
            return job_available_ || should_stop_ || mining_paused_; 
        });
        
        if (should_stop_) break;
        
        if (mining_paused_) {
            worker->state = WorkerState::PAUSED;
            continue;
        }
        
        if (!job_available_) continue;
        
        // 复制当前任务
        StratumJob job = current_job_;
        lock.unlock();
        
        // 开始工作
        worker->state = WorkerState::WORKING;
        process_mining_work(worker, job);
    }
    
    worker->state = WorkerState::STOPPED;
}

bool MiningEngine::process_mining_work(WorkerInfo* worker, const StratumJob& job) {
    SHA256Context ctx;
    uint8_t hash[32];
    uint32_t nonce_start = worker->start_nonce;
    uint32_t nonce_end = worker->end_nonce;
    uint64_t hashes_in_batch = 0;
    
    auto batch_start = std::chrono::steady_clock::now();
    
    for (uint32_t nonce = nonce_start; nonce < nonce_end && !should_stop_ && !mining_paused_; nonce++) {
        // 构建区块头
        std::vector<uint8_t> block_header = StratumUtils::build_block_header(
            job, stratum_client_->get_subscription().extranonce1, 
            std::to_string(worker->worker_id), nonce);
        
        // 计算SHA256哈希
        if (config_.enable_optimization) {
            SHA256Core::sha256_double_hash(block_header.data(), block_header.size(), hash);
        } else {
            SHA256Core::sha256_transform(block_header.data(), ctx.state);
            memcpy(hash, ctx.state, 32);
        }
        
        hashes_in_batch++;
        worker->hashes_done++;
        stats_.total_hashes++;
        
        // 检查是否满足目标难度
        if (SHA256Core::check_target(hash, job.target_bytes)) {
            if (check_and_submit_share(job, nonce, hash, worker)) {
                stats_.valid_shares++;
            } else {
                stats_.invalid_shares++;
            }
        }
        
        // 每批次更新统计信息
        if (hashes_in_batch >= config_.batch_size) {
            auto batch_end = std::chrono::steady_clock::now();
            auto duration = std::chrono::duration<double>(batch_end - batch_start).count();
            worker->hashrate = hashes_in_batch / duration;
            
            hashes_in_batch = 0;
            batch_start = batch_end;
            
            // 获取新的nonce范围
            uint32_t range_size = nonce_end - nonce_start;
            nonce_start = get_next_nonce_range(worker->worker_id, range_size);
            nonce_end = nonce_start + range_size;
            nonce = nonce_start;
        }
    }
    
    return true;
}

bool MiningEngine::check_and_submit_share(const StratumJob& job, uint32_t nonce, 
                                         const uint8_t* hash, WorkerInfo* worker) {
    // 计算share难度
    double share_difficulty = MiningUtils::calculate_share_difficulty(hash);
    
    // 检查是否满足最小难度要求
    if (share_difficulty < config_.target_difficulty) {
        return false;
    }
    
    // 创建share
    StratumShare share;
    share.job_id = job.job_id;
    share.extranonce2 = std::to_string(worker->worker_id);
    share.ntime = job.ntime;
    share.nonce = nonce;
    share.worker_name = config_.username;
    
    // 提交share
    bool success = stratum_client_->submit_share(share);
    
    // 触发回调
    std::lock_guard<std::mutex> lock(callback_mutex_);
    if (share_found_callback_) {
        share_found_callback_(share, success);
    }
    
    return success;
}

void MiningEngine::on_job_received(const StratumJob& job) {
    std::lock_guard<std::mutex> lock(job_mutex_);
    current_job_ = job;
    job_available_ = true;
    
    // 重新分配nonce范围
    distribute_nonce_ranges();
    
    job_condition_.notify_all();
    handle_status_change("收到新的挖矿任务: " + job.job_id);
}

void MiningEngine::on_difficulty_changed(double difficulty) {
    config_.target_difficulty = difficulty;
    handle_status_change("难度已更新: " + std::to_string(difficulty));
}

void MiningEngine::on_stratum_error(const std::string& error) {
    handle_error("Stratum错误: " + error);
}

void MiningEngine::on_connection_status(bool connected) {
    if (connected) {
        handle_status_change("已连接到矿池");
    } else {
        handle_status_change("与矿池连接断开");
        if (mining_active_) {
            pause_mining();
        }
    }
}

void MiningEngine::stats_update_loop() {
    while (stats_thread_running_) {
        std::this_thread::sleep_for(std::chrono::seconds(config_.stats_interval_seconds));
        
        if (stats_thread_running_) {
            update_hashrate();
            trigger_stats_callback();
        }
    }
}

void MiningEngine::update_hashrate() {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    
    auto now = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration<double>(now - stats_.last_update).count();
    
    if (duration > 0) {
        // 计算当前算力
        uint64_t total_hashes = 0;
        {
            std::lock_guard<std::mutex> worker_lock(workers_mutex_);
            for (const auto& worker : workers_) {
                total_hashes += worker->hashes_done;
            }
        }
        
        stats_.current_hashrate = total_hashes / duration;
        
        // 计算平均算力
        auto total_duration = std::chrono::duration<double>(now - stats_.start_time).count();
        if (total_duration > 0) {
            stats_.average_hashrate = stats_.total_hashes / total_duration;
        }
        
        stats_.last_update = now;
    }
}

void MiningEngine::trigger_stats_callback() {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    if (stats_callback_) {
        stats_callback_(stats_);
    }
}

void MiningEngine::distribute_nonce_ranges() {
    if (workers_.empty()) return;
    
    uint32_t total_range = 0xFFFFFFFF;
    uint32_t range_per_worker = total_range / workers_.size();
    
    for (size_t i = 0; i < workers_.size(); ++i) {
        workers_[i]->start_nonce = i * range_per_worker;
        workers_[i]->end_nonce = (i == workers_.size() - 1) ? 
            total_range : (i + 1) * range_per_worker;
    }
}

uint32_t MiningEngine::get_next_nonce_range(int worker_id, uint32_t range_size) {
    static std::atomic<uint32_t> global_nonce(0);
    return global_nonce.fetch_add(range_size);
}

void MiningEngine::handle_error(const std::string& error) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    if (error_callback_) {
        error_callback_(error);
    }
}

void MiningEngine::handle_status_change(const std::string& status) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    if (status_callback_) {
        status_callback_(status);
    }
}

void MiningEngine::auto_reconnect_loop() {
    while (reconnect_thread_running_) {
        std::this_thread::sleep_for(std::chrono::seconds(config_.reconnect_delay_seconds));
        
        if (!reconnect_thread_running_) break;
        
        if (!stratum_client_->is_connected() && config_.auto_reconnect) {
            handle_status_change("尝试重新连接到矿池...");
            if (connect_to_pool()) {
                if (mining_active_ && mining_paused_) {
                    resume_mining();
                }
            }
        }
    }
}

// MiningUtils实现
namespace MiningUtils {
    bool has_avx2_support() {
#ifdef _WIN32
        int cpuInfo[4];
        __cpuid(cpuInfo, 7);
        return (cpuInfo[1] & (1 << 5)) != 0;
#else
        unsigned int eax, ebx, ecx, edx;
        if (__get_cpuid_count(7, 0, &eax, &ebx, &ecx, &edx)) {
            return (ebx & (1 << 5)) != 0;
        }
        return false;
#endif
    }
    
    bool has_sse2_support() {
#ifdef _WIN32
        int cpuInfo[4];
        __cpuid(cpuInfo, 1);
        return (cpuInfo[3] & (1 << 26)) != 0;
#else
        unsigned int eax, ebx, ecx, edx;
        if (__get_cpuid(1, &eax, &ebx, &ecx, &edx)) {
            return (edx & (1 << 26)) != 0;
        }
        return false;
#endif
    }
    
    int get_optimal_thread_count() {
        int cores = std::thread::hardware_concurrency();
        if (cores == 0) return 4;
        
        // 对于挖矿，通常使用所有可用核心
        return cores;
    }
    
    void set_thread_affinity(std::thread& thread, int cpu_id) {
#ifdef _WIN32
        HANDLE handle = thread.native_handle();
        SetThreadAffinityMask(handle, 1ULL << cpu_id);
#else
        pthread_t handle = thread.native_handle();
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(cpu_id, &cpuset);
        pthread_setaffinity_np(handle, sizeof(cpu_set_t), &cpuset);
#endif
    }
    
    void set_thread_priority(std::thread& thread, int priority) {
#ifdef _WIN32
        HANDLE handle = thread.native_handle();
        SetThreadPriority(handle, priority);
#else
        pthread_t handle = thread.native_handle();
        struct sched_param param;
        param.sched_priority = priority;
        pthread_setschedparam(handle, SCHED_FIFO, &param);
#endif
    }
    
    std::string format_hashrate(double hashrate) {
        const char* units[] = {"H/s", "KH/s", "MH/s", "GH/s", "TH/s"};
        int unit_index = 0;
        
        while (hashrate >= 1000.0 && unit_index < 4) {
            hashrate /= 1000.0;
            unit_index++;
        }
        
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(2) << hashrate << " " << units[unit_index];
        return oss.str();
    }
    
    std::string format_duration(std::chrono::seconds duration) {
        auto hours = std::chrono::duration_cast<std::chrono::hours>(duration);
        auto minutes = std::chrono::duration_cast<std::chrono::minutes>(duration % std::chrono::hours(1));
        auto seconds = duration % std::chrono::minutes(1);
        
        std::ostringstream oss;
        oss << std::setfill('0') << std::setw(2) << hours.count() << ":"
            << std::setw(2) << minutes.count() << ":"
            << std::setw(2) << seconds.count();
        return oss.str();
    }
    
    std::string format_timestamp(std::chrono::system_clock::time_point time) {
        auto time_t = std::chrono::system_clock::to_time_t(time);
        std::ostringstream oss;
        oss << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S");
        return oss.str();
    }
    
    double calculate_share_difficulty(const uint8_t* hash) {
        // 计算哈希的难度值
        uint64_t hash_value = 0;
        for (int i = 0; i < 8; ++i) {
            hash_value = (hash_value << 8) | hash[31 - i];
        }
        
        if (hash_value == 0) return 1.0;
        return static_cast<double>(0x00000000FFFF0000ULL) / hash_value;
    }
    
    bool meets_target_difficulty(const uint8_t* hash, double target_difficulty) {
        double share_difficulty = calculate_share_difficulty(hash);
        return share_difficulty >= target_difficulty;
    }
    
    bool load_config_from_file(const std::string& filename, MiningConfig& config) {
        std::ifstream file(filename);
        if (!file.is_open()) {
            return false;
        }
        
        std::string line;
        while (std::getline(file, line)) {
            size_t pos = line.find('=');
            if (pos == std::string::npos) continue;
            
            std::string key = line.substr(0, pos);
            std::string value = line.substr(pos + 1);
            
            if (key == "pool_host") config.pool_host = value;
            else if (key == "pool_port") config.pool_port = std::stoi(value);
            else if (key == "username") config.username = value;
            else if (key == "password") config.password = value;
            else if (key == "thread_count") config.thread_count = std::stoi(value);
            else if (key == "batch_size") config.batch_size = std::stoul(value);
            else if (key == "enable_avx2") config.enable_avx2 = (value == "true");
            else if (key == "enable_optimization") config.enable_optimization = (value == "true");
        }
        
        return true;
    }
    
    bool save_config_to_file(const std::string& filename, const MiningConfig& config) {
        std::ofstream file(filename);
        if (!file.is_open()) {
            return false;
        }
        
        file << "pool_host=" << config.pool_host << "\n";
        file << "pool_port=" << config.pool_port << "\n";
        file << "username=" << config.username << "\n";
        file << "password=" << config.password << "\n";
        file << "thread_count=" << config.thread_count << "\n";
        file << "batch_size=" << config.batch_size << "\n";
        file << "enable_avx2=" << (config.enable_avx2 ? "true" : "false") << "\n";
        file << "enable_optimization=" << (config.enable_optimization ? "true" : "false") << "\n";
        
        return true;
    }
} 