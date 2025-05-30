#include "stratum_client.h"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <cstring>
#include <chrono>
#include <algorithm>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
    #define close closesocket
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <netdb.h>
    #include <unistd.h>
#endif

// Simple JSON parser (lightweight implementation)
class SimpleJSON {
public:
    static std::string get_string(const std::string& json, const std::string& key) {
        std::string search = "\"" + key + "\":\"";
        size_t pos = json.find(search);
        if (pos == std::string::npos) return "";
        
        pos += search.length();
        size_t end = json.find("\"", pos);
        if (end == std::string::npos) return "";
        
        return json.substr(pos, end - pos);
    }
    
    static double get_number(const std::string& json, const std::string& key) {
        std::string search = "\"" + key + "\":";
        size_t pos = json.find(search);
        if (pos == std::string::npos) return 0.0;
        
        pos += search.length();
        while (pos < json.length() && (json[pos] == ' ' || json[pos] == '\t')) pos++;
        
        size_t end = pos;
        while (end < json.length() && (std::isdigit(json[end]) || json[end] == '.' || json[end] == 'e' || json[end] == 'E' || json[end] == '-' || json[end] == '+')) {
            end++;
        }
        
        if (end > pos) {
            return std::stod(json.substr(pos, end - pos));
        }
        return 0.0;
    }
    
    static std::vector<std::string> get_array(const std::string& json, const std::string& key) {
        std::vector<std::string> result;
        std::string search = "\"" + key + "\":[";
        size_t pos = json.find(search);
        if (pos == std::string::npos) return result;
        
        pos += search.length();
        size_t end = json.find("]", pos);
        if (end == std::string::npos) return result;
        
        std::string array_content = json.substr(pos, end - pos);
        
        // Simple array element parsing
        size_t start = 0;
        while (start < array_content.length()) {
            size_t quote_start = array_content.find("\"", start);
            if (quote_start == std::string::npos) break;
            
            size_t quote_end = array_content.find("\"", quote_start + 1);
            if (quote_end == std::string::npos) break;
            
            result.push_back(array_content.substr(quote_start + 1, quote_end - quote_start - 1));
            start = quote_end + 1;
        }
        
        return result;
    }
    
    static bool get_bool(const std::string& json, const std::string& key) {
        std::string search = "\"" + key + "\":";
        size_t pos = json.find(search);
        if (pos == std::string::npos) return false;
        
        pos += search.length();
        while (pos < json.length() && (json[pos] == ' ' || json[pos] == '\t')) pos++;
        
        return json.substr(pos, 4) == "true";
    }
};

// StratumClient implementation
StratumClient::StratumClient() 
    : socket_fd_(-1), port_(0), connected_(false), current_difficulty_(1.0), next_id_(1),
      shares_submitted_(0), shares_accepted_(0), shares_rejected_(0),
      bytes_sent_(0), bytes_received_(0), stop_flag_(false) {
    
#ifdef _WIN32
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif
}

StratumClient::~StratumClient() {
    disconnect();
    stop_message_loop();
    
#ifdef _WIN32
    WSACleanup();
#endif
}

bool StratumClient::connect(const std::string& host, int port) {
    if (connected_) {
        disconnect();
    }
    
    // Parse URL if it contains protocol prefix
    std::string actual_host = host;
    if (host.find("stratum+tcp://") == 0) {
        actual_host = host.substr(14); // Remove "stratum+tcp://" prefix
    } else if (host.find("stratum://") == 0) {
        actual_host = host.substr(10); // Remove "stratum://" prefix
    }
    
    // Remove any trailing path or parameters
    size_t slash_pos = actual_host.find('/');
    if (slash_pos != std::string::npos) {
        actual_host = actual_host.substr(0, slash_pos);
    }
    
    host_ = actual_host;
    port_ = port;
    
    if (!create_socket()) {
        return false;
    }
    
    // Resolve hostname
    struct hostent* he = gethostbyname(actual_host.c_str());
    if (!he) {
        close_socket();
        return false;
    }
    
    // Set server address
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    memcpy(&server_addr.sin_addr, he->h_addr_list[0], he->h_length);
    
    // Connect to server
    if (::connect(socket_fd_, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        close_socket();
        return false;
    }
    
    connected_ = true;
    
    // Trigger connection callback
    if (connect_callback_) {
        connect_callback_(true);
    }
    
    return true;
}

void StratumClient::disconnect() {
    if (connected_) {
        connected_ = false;
        close_socket();
        
        if (connect_callback_) {
            connect_callback_(false);
        }
    }
}

bool StratumClient::is_connected() const {
    return connected_;
}

bool StratumClient::subscribe(const std::string& user_agent) {
    if (!connected_) return false;
    
    std::string message = create_subscribe_message();
    return send_message(message);
}

bool StratumClient::authorize(const std::string& username, const std::string& password) {
    if (!connected_) return false;
    
    std::string message = create_authorize_message(username, password);
    return send_message(message);
}

bool StratumClient::submit_share(const StratumShare& share) {
    if (!connected_) return false;
    
    std::string message = create_submit_message(share);
    bool result = send_message(message);
    
    if (result) {
        shares_submitted_++;
    }
    
    return result;
}

bool StratumClient::suggest_difficulty(double difficulty) {
    if (!connected_) return false;
    
    std::string message = create_suggest_difficulty_message(difficulty);
    return send_message(message);
}

void StratumClient::set_job_callback(JobCallback callback) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    job_callback_ = callback;
}

void StratumClient::set_difficulty_callback(DifficultyCallback callback) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    difficulty_callback_ = callback;
}

void StratumClient::set_error_callback(ErrorCallback callback) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    error_callback_ = callback;
}

void StratumClient::set_connect_callback(ConnectCallback callback) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    connect_callback_ = callback;
}

void StratumClient::start_message_loop() {
    if (!message_thread_.joinable()) {
        stop_flag_ = false;
        message_thread_ = std::thread(&StratumClient::message_loop, this);
    }
}

void StratumClient::stop_message_loop() {
    stop_flag_ = true;
    
    // Give some time for the thread to exit gracefully
    if (message_thread_.joinable()) {
        auto start = std::chrono::steady_clock::now();
        while (message_thread_.joinable() && 
               std::chrono::duration_cast<std::chrono::seconds>(
                   std::chrono::steady_clock::now() - start).count() < 5) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        
        if (message_thread_.joinable()) {
            message_thread_.join();
        }
    }
}

bool StratumClient::create_socket() {
    socket_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd_ < 0) {
        return false;
    }
    
    // Set socket options
    int opt = 1;
    setsockopt(socket_fd_, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt));
    
    return true;
}

void StratumClient::close_socket() {
    if (socket_fd_ >= 0) {
        close(socket_fd_);
        socket_fd_ = -1;
    }
}

bool StratumClient::send_message(const std::string& message) {
    if (!connected_ || socket_fd_ < 0) return false;
    
    std::lock_guard<std::mutex> lock(send_mutex_);
    
    std::string full_message = message + "\n";
    const char* data = full_message.c_str();
    size_t length = full_message.length();
    
    size_t sent = 0;
    while (sent < length) {
        int result = send(socket_fd_, data + sent, length - sent, 0);
        if (result <= 0) {
            handle_connection_lost();
            return false;
        }
        sent += result;
    }
    
    bytes_sent_ += length;
    return true;
}

std::string StratumClient::receive_line() {
    if (!connected_ || socket_fd_ < 0) return "";
    
    std::string line;
    char buffer[1];
    
    while (connected_) {
        int result = recv(socket_fd_, buffer, 1, 0);
        if (result <= 0) {
            handle_connection_lost();
            break;
        }
        
        bytes_received_++;
        
        if (buffer[0] == '\n') {
            break;
        } else if (buffer[0] != '\r') {
            line += buffer[0];
        }
    }
    
    return line;
}

void StratumClient::message_loop() {
    while (!stop_flag_ && connected_) {
        try {
            std::string line = receive_line();
            if (!line.empty() && connected_) {
                parse_json_response(line);
            } else if (!connected_) {
                break;
            }
        } catch (const std::exception& e) {
            handle_error("Message loop error: " + std::string(e.what()));
            break;
        }
        
        // Short sleep to avoid high CPU usage
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

bool StratumClient::parse_json_response(const std::string& line) {
    try {
        // Check if it's a method call
        if (line.find("\"method\"") != std::string::npos) {
            std::string method_str = SimpleJSON::get_string(line, "method");
            StratumMethod method = parse_method(method_str);
            
            switch (method) {
                case StratumMethod::MINING_NOTIFY:
                    return handle_mining_notify(line);
                case StratumMethod::MINING_SET_DIFFICULTY:
                    return handle_mining_set_difficulty(line);
                default:
                    break;
            }
        } else if (line.find("\"result\"") != std::string::npos || line.find("\"error\"") != std::string::npos) {
            return handle_response(line);
        }
    } catch (const std::exception& e) {
        handle_error("JSON parsing error: " + std::string(e.what()));
        return false;
    }
    
    return true;
}

StratumMethod StratumClient::parse_method(const std::string& method_str) {
    if (method_str == "mining.notify") return StratumMethod::MINING_NOTIFY;
    if (method_str == "mining.set_difficulty") return StratumMethod::MINING_SET_DIFFICULTY;
    if (method_str == "mining.set_extranonce") return StratumMethod::MINING_SET_EXTRANONCE;
    if (method_str == "client.reconnect") return StratumMethod::CLIENT_RECONNECT;
    return StratumMethod::UNKNOWN;
}

bool StratumClient::handle_mining_notify(const std::string& json) {
    try {
        // Add debug output
        handle_error("Received mining.notify: " + json.substr(0, 200) + "...");
        
        // Parse mining.notify parameters
        std::vector<std::string> params = SimpleJSON::get_array(json, "params");
        if (params.size() < 9) {
            handle_error("Invalid mining.notify: insufficient parameters (" + std::to_string(params.size()) + ")");
            return false;
        }
        
        StratumJob new_job;
        new_job.job_id = params[0];
        new_job.prev_block_hash = params[1];
        new_job.coinbase1 = params[2];
        new_job.coinbase2 = params[3];
        
        // Parse merkle branches (needs more complex parsing here)
        // Simplified handling, should actually parse nested arrays
        
        new_job.version = params[5];
        new_job.nbits = params[6];
        new_job.ntime = params[7];
        new_job.clean_jobs = (params[8] == "true");
        
        // Update current job
        current_job_ = new_job;
        update_target_from_difficulty(current_difficulty_);
        
        handle_error("Successfully parsed job: " + new_job.job_id + ", clean_jobs=" + (new_job.clean_jobs ? "true" : "false"));
        
        // Get callback outside of lock to avoid deadlock
        JobCallback callback = nullptr;
        {
            std::lock_guard<std::mutex> lock(callback_mutex_);
            callback = job_callback_;
        }
        
        // Trigger job callback outside of lock
        if (callback) {
            handle_error("Triggering job callback for job: " + new_job.job_id);
            callback(current_job_);
        } else {
            handle_error("No job callback set!");
        }
        
        return true;
    } catch (const std::exception& e) {
        handle_error("Failed to parse mining.notify: " + std::string(e.what()));
        return false;
    }
}

bool StratumClient::handle_mining_set_difficulty(const std::string& json) {
    try {
        std::vector<std::string> params = SimpleJSON::get_array(json, "params");
        if (params.empty()) return false;
        
        double new_difficulty = std::stod(params[0]);
        current_difficulty_ = new_difficulty;
        
        // Update target value
        update_target_from_difficulty(new_difficulty);
        
        // Get callback outside of lock to avoid deadlock
        DifficultyCallback callback = nullptr;
        {
            std::lock_guard<std::mutex> lock(callback_mutex_);
            callback = difficulty_callback_;
        }
        
        // Trigger difficulty callback outside of lock
        if (callback) {
            callback(new_difficulty);
        }
        
        return true;
    } catch (const std::exception& e) {
        handle_error("Failed to parse mining.set_difficulty: " + std::string(e.what()));
        return false;
    }
}

bool StratumClient::handle_response(const std::string& json) {
    try {
        // Check for errors
        if (json.find("\"error\"") != std::string::npos && json.find("null") == std::string::npos) {
            std::string error_msg = SimpleJSON::get_string(json, "message");
            if (error_msg.empty()) {
                error_msg = "Unknown error";
            }
            
            shares_rejected_++;
            handle_error("Share rejected: " + error_msg);
            return false;
        }
        
        // Success response
        if (json.find("\"result\"") != std::string::npos) {
            // Check if it's a subscription response
            if (json.find("[[") != std::string::npos) {
                // Parse subscription response
                std::vector<std::string> result = SimpleJSON::get_array(json, "result");
                if (result.size() >= 2) {
                    subscription_.subscription_id = result[0];
                    subscription_.extranonce1 = result[1];
                    if (result.size() >= 3) {
                        subscription_.extranonce2_size = std::stoi(result[2]);
                    }
                }
            } else {
                // Possibly a successful share submission response
                shares_accepted_++;
            }
            return true;
        }
        
        return true;
    } catch (const std::exception& e) {
        handle_error("Failed to parse response: " + std::string(e.what()));
        return false;
    }
}

std::string StratumClient::create_subscribe_message() {
    std::ostringstream oss;
    oss << "{\"id\":" << get_next_id() 
        << ",\"method\":\"mining.subscribe\""
        << ",\"params\":[\"PCMiner/1.0\"]}";
    return oss.str();
}

std::string StratumClient::create_authorize_message(const std::string& username, const std::string& password) {
    std::ostringstream oss;
    oss << "{\"id\":" << get_next_id()
        << ",\"method\":\"mining.authorize\""
        << ",\"params\":[\"" << username << "\",\"" << password << "\"]}";
    return oss.str();
}

std::string StratumClient::create_submit_message(const StratumShare& share) {
    std::ostringstream oss;
    oss << "{\"id\":" << get_next_id()
        << ",\"method\":\"mining.submit\""
        << ",\"params\":[\"" << share.worker_name << "\",\"" << share.job_id 
        << "\",\"" << share.extranonce2 << "\",\"" << share.ntime 
        << "\",\"" << std::hex << share.nonce << "\"]}";
    return oss.str();
}

std::string StratumClient::create_suggest_difficulty_message(double difficulty) {
    std::ostringstream oss;
    oss << "{\"id\":" << get_next_id()
        << ",\"method\":\"mining.suggest_difficulty\""
        << ",\"params\":[" << difficulty << "]}";
    return oss.str();
}

uint32_t StratumClient::get_next_id() {
    return next_id_++;
}

void StratumClient::update_target_from_difficulty(double difficulty) {
    StratumUtils::difficulty_to_target(difficulty, current_job_.target_bytes);
    current_job_.target = static_cast<uint32_t>(0xFFFFFFFF / difficulty);
}

std::string StratumClient::hex_to_string(const std::string& hex) {
    return StratumUtils::hex_encode(StratumUtils::hex_decode(hex));
}

std::string StratumClient::string_to_hex(const std::string& str) {
    return StratumUtils::hex_encode(reinterpret_cast<const uint8_t*>(str.c_str()), str.length());
}

void StratumClient::handle_error(const std::string& error) {
    ErrorCallback callback = nullptr;
    {
        std::lock_guard<std::mutex> lock(callback_mutex_);
        callback = error_callback_;
    }
    
    if (callback) {
        callback(error);
    }
}

void StratumClient::handle_connection_lost() {
    if (connected_) {
        connected_ = false;
        close_socket();
        
        // Get callback outside of any locks to avoid deadlock
        ConnectCallback callback = nullptr;
        {
            std::lock_guard<std::mutex> lock(callback_mutex_);
            callback = connect_callback_;
        }
        
        if (callback) {
            callback(false);
        }
    }
}

// StratumUtils implementation
namespace StratumUtils {
    void difficulty_to_target(double difficulty, uint8_t* target) {
        // Calculate target from difficulty
        // target = max_target / difficulty
        
        // Maximum target (difficulty 1)
        uint8_t max_target[32] = {
            0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0x00, 0x1d,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
        };
        
        // Simple implementation: scale max_target by difficulty
        memcpy(target, max_target, 32);
        
        // Adjust target based on difficulty (simplified)
        if (difficulty > 1.0) {
            uint64_t scale = static_cast<uint64_t>(difficulty);
            for (int i = 31; i >= 0 && scale > 1; i--) {
                uint64_t val = target[i];
                val /= scale;
                target[i] = static_cast<uint8_t>(val);
                scale /= 256;
                if (scale <= 1) break;
            }
        }
    }
    
    bool check_target(const uint8_t* hash, const uint8_t* target) {
        // Compare hash with target (hash must be less than target)
        for (int i = 0; i < 32; i++) {
            if (hash[i] < target[i]) return true;
            if (hash[i] > target[i]) return false;
        }
        return false; // Equal is not valid
    }
    
    std::string bytes_to_hex(const uint8_t* data, size_t len) {
        std::stringstream ss;
        ss << std::hex << std::setfill('0');
        for (size_t i = 0; i < len; i++) {
            ss << std::setw(2) << static_cast<unsigned>(data[i]);
        }
        return ss.str();
    }
    
    void hex_to_bytes(const std::string& hex, uint8_t* data) {
        for (size_t i = 0; i < hex.length(); i += 2) {
            std::string byte_str = hex.substr(i, 2);
            data[i/2] = static_cast<uint8_t>(std::stoul(byte_str, nullptr, 16));
        }
    }
    
    uint32_t reverse_bytes(uint32_t value) {
        return ((value & 0xFF000000) >> 24) |
               ((value & 0x00FF0000) >> 8)  |
               ((value & 0x0000FF00) << 8)  |
               ((value & 0x000000FF) << 24);
    }
    
    void reverse_bytes(uint8_t* data, size_t length) {
        for (size_t i = 0; i < length / 2; ++i) {
            std::swap(data[i], data[length - 1 - i]);
        }
    }
    
    double target_to_difficulty(const uint8_t* target) {
        uint64_t target_value = *reinterpret_cast<const uint64_t*>(target);
        if (target_value == 0) return 1.0;
        return static_cast<double>(0x00000000FFFF0000ULL) / target_value;
    }
    
    std::vector<uint8_t> hex_decode(const std::string& hex) {
        std::vector<uint8_t> result;
        for (size_t i = 0; i < hex.length(); i += 2) {
            if (i + 1 < hex.length()) {
                uint8_t byte = static_cast<uint8_t>(std::stoi(hex.substr(i, 2), nullptr, 16));
                result.push_back(byte);
            }
        }
        return result;
    }
    
    std::string hex_encode(const uint8_t* data, size_t length) {
        std::ostringstream oss;
        oss << std::hex << std::setfill('0');
        for (size_t i = 0; i < length; ++i) {
            oss << std::setw(2) << static_cast<int>(data[i]);
        }
        return oss.str();
    }
    
    std::string hex_encode(const std::vector<uint8_t>& data) {
        return hex_encode(data.data(), data.size());
    }
    
    std::string calculate_merkle_root(const std::string& coinbase_hash, const std::vector<std::string>& merkle_branches) {
        std::string current_hash = coinbase_hash;
        
        for (const std::string& branch : merkle_branches) {
            // Simplified merkle calculation, actual implementation needs SHA256 double hash
            current_hash = current_hash + branch;
        }
        
        return current_hash;
    }
    
    std::vector<uint8_t> build_block_header(const StratumJob& job, const std::string& extranonce1, 
                                           const std::string& extranonce2, uint32_t nonce) {
        std::vector<uint8_t> header(80);
        
        // Build block header (simplified version)
        // Actual implementation needs correct byte order and field arrangement
        
        // Version (4 bytes)
        uint32_t version = std::stoul(job.version, nullptr, 16);
        memcpy(header.data(), &version, 4);
        
        // Previous block hash (32 bytes)
        std::vector<uint8_t> prev_hash = hex_decode(job.prev_block_hash);
        if (prev_hash.size() >= 32) {
            memcpy(header.data() + 4, prev_hash.data(), 32);
        }
        
        // Merkle root (32 bytes) - needs calculation
        // Simplified handling here
        
        // Timestamp (4 bytes)
        uint32_t timestamp = std::stoul(job.ntime, nullptr, 16);
        memcpy(header.data() + 68, &timestamp, 4);
        
        // Difficulty target (4 bytes)
        uint32_t bits = std::stoul(job.nbits, nullptr, 16);
        memcpy(header.data() + 72, &bits, 4);
        
        // Nonce (4 bytes)
        memcpy(header.data() + 76, &nonce, 4);
        
        return header;
    }
} 