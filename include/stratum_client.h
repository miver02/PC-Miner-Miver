#pragma once

#include <string>
#include <vector>
#include <memory>
#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>

// Forward declarations
struct sockaddr_in;

// Stratum protocol related structures
struct StratumSubscription {
    std::string subscription_id;
    std::string extranonce1;
    int extranonce2_size;
    std::string session_id;
    
    StratumSubscription() : extranonce2_size(0) {}
};

struct StratumJob {
    std::string job_id;
    std::string prev_block_hash;
    std::string coinbase1;
    std::string coinbase2;
    std::vector<std::string> merkle_branches;
    std::string version;
    std::string nbits;
    std::string ntime;
    bool clean_jobs;
    uint32_t target;
    uint8_t target_bytes[32];
    
    StratumJob() : clean_jobs(false), target(0) {
        memset(target_bytes, 0xFF, 32);
    }
};

struct StratumShare {
    std::string job_id;
    std::string extranonce2;
    std::string ntime;
    uint32_t nonce;
    std::string worker_name;
    
    StratumShare() : nonce(0) {}
};

// Stratum method enumeration
enum class StratumMethod {
    UNKNOWN,
    MINING_NOTIFY,
    MINING_SET_DIFFICULTY,
    MINING_SET_EXTRANONCE,
    CLIENT_RECONNECT,
    RESPONSE_SUCCESS,
    RESPONSE_ERROR
};

// Stratum client class
class StratumClient {
public:
    // Callback function type
    using JobCallback = std::function<void(const StratumJob&)>;
    using DifficultyCallback = std::function<void(double)>;
    using ErrorCallback = std::function<void(const std::string&)>;
    using ConnectCallback = std::function<void(bool)>;
    
    // Constructor
    StratumClient();
    ~StratumClient();
    
    // Basic connection management
    bool connect(const std::string& host, int port);
    void disconnect();
    bool is_connected() const;
    
    // Stratum protocol operations
    bool subscribe(const std::string& user_agent = "PCMiner/1.0");
    bool authorize(const std::string& username, const std::string& password = "x");
    bool submit_share(const StratumShare& share);
    bool suggest_difficulty(double difficulty);
    
    // Set callback functions
    void set_job_callback(JobCallback callback);
    void set_difficulty_callback(DifficultyCallback callback);
    void set_error_callback(ErrorCallback callback);
    void set_connect_callback(ConnectCallback callback);
    
    // Get current status
    const StratumSubscription& get_subscription() const { return subscription_; }
    const StratumJob& get_current_job() const { return current_job_; }
    double get_current_difficulty() const { return current_difficulty_; }
    
    // Statistics
    uint64_t get_shares_submitted() const { return shares_submitted_; }
    uint64_t get_shares_accepted() const { return shares_accepted_; }
    uint64_t get_shares_rejected() const { return shares_rejected_; }
    
    // Network statistics
    uint64_t get_bytes_sent() const { return bytes_sent_; }
    uint64_t get_bytes_received() const { return bytes_received_; }
    
    // Start/stop message processing thread
    void start_message_loop();
    void stop_message_loop();
    
private:
    // Network related
    int socket_fd_;
    std::string host_;
    int port_;
    std::atomic<bool> connected_;
    
    // Stratum status
    StratumSubscription subscription_;
    StratumJob current_job_;
    double current_difficulty_;
    uint32_t next_id_;
    
    // Statistics
    std::atomic<uint64_t> shares_submitted_;
    std::atomic<uint64_t> shares_accepted_;
    std::atomic<uint64_t> shares_rejected_;
    std::atomic<uint64_t> bytes_sent_;
    std::atomic<uint64_t> bytes_received_;
    
    // Callback functions
    JobCallback job_callback_;
    DifficultyCallback difficulty_callback_;
    ErrorCallback error_callback_;
    ConnectCallback connect_callback_;
    
    // Thread management
    std::thread message_thread_;
    std::atomic<bool> stop_flag_;
    std::mutex send_mutex_;
    std::mutex callback_mutex_;
    
    // Internal methods
    bool create_socket();
    void close_socket();
    bool send_message(const std::string& message);
    std::string receive_line();
    void message_loop();
    
    // JSON processing
    bool parse_json_response(const std::string& line);
    StratumMethod parse_method(const std::string& method_str);
    bool handle_mining_notify(const std::string& json);
    bool handle_mining_set_difficulty(const std::string& json);
    bool handle_response(const std::string& json);
    
    // Utility methods
    std::string create_subscribe_message();
    std::string create_authorize_message(const std::string& username, const std::string& password);
    std::string create_submit_message(const StratumShare& share);
    std::string create_suggest_difficulty_message(double difficulty);
    
    uint32_t get_next_id();
    void update_target_from_difficulty(double difficulty);
    std::string hex_to_string(const std::string& hex);
    std::string string_to_hex(const std::string& str);
    
    // Error handling
    void handle_error(const std::string& error);
    void handle_connection_lost();
    
    // Disable copy
    StratumClient(const StratumClient&) = delete;
    StratumClient& operator=(const StratumClient&) = delete;
};

// Utility functions
namespace StratumUtils {
    // Difficulty conversion
    void difficulty_to_target(double difficulty, uint8_t* target);
    double target_to_difficulty(const uint8_t* target);
    
    // Target checking
    bool check_target(const uint8_t* hash, const uint8_t* target);
    
    // Hexadecimal conversion
    std::vector<uint8_t> hex_decode(const std::string& hex);
    std::string hex_encode(const uint8_t* data, size_t length);
    std::string hex_encode(const std::vector<uint8_t>& data);
    
    // Byte order conversion
    uint32_t reverse_bytes(uint32_t value);
    void reverse_bytes(uint8_t* data, size_t length);
    
    // Merkle root calculation
    std::string calculate_merkle_root(const std::string& coinbase_hash, const std::vector<std::string>& merkle_branches);
    
    // Block header construction
    std::vector<uint8_t> build_block_header(const StratumJob& job, const std::string& extranonce1, 
                                           const std::string& extranonce2, uint32_t nonce);
} 