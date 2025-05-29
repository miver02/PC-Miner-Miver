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

// 简单的JSON解析器（轻量级实现）
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
        
        // 简单解析数组元素
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

// StratumClient实现
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
    
    host_ = host;
    port_ = port;
    
    if (!create_socket()) {
        return false;
    }
    
    // 解析主机名
    struct hostent* he = gethostbyname(host.c_str());
    if (!he) {
        close_socket();
        return false;
    }
    
    // 设置服务器地址
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    memcpy(&server_addr.sin_addr, he->h_addr_list[0], he->h_length);
    
    // 连接到服务器
    if (::connect(socket_fd_, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        close_socket();
        return false;
    }
    
    connected_ = true;
    
    // 触发连接回调
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
    if (message_thread_.joinable()) {
        message_thread_.join();
    }
}

bool StratumClient::create_socket() {
    socket_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd_ < 0) {
        return false;
    }
    
    // 设置socket选项
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
        std::string line = receive_line();
        if (!line.empty()) {
            parse_json_response(line);
        }
        
        // 短暂休眠避免CPU占用过高
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

bool StratumClient::parse_json_response(const std::string& line) {
    try {
        // 检查是否是方法调用
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
        handle_error("JSON解析错误: " + std::string(e.what()));
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
        // 解析mining.notify参数
        std::vector<std::string> params = SimpleJSON::get_array(json, "params");
        if (params.size() < 9) return false;
        
        StratumJob new_job;
        new_job.job_id = params[0];
        new_job.prev_block_hash = params[1];
        new_job.coinbase1 = params[2];
        new_job.coinbase2 = params[3];
        
        // 解析merkle分支（这里需要更复杂的解析）
        // 简化处理，实际应该解析嵌套数组
        
        new_job.version = params[5];
        new_job.nbits = params[6];
        new_job.ntime = params[7];
        new_job.clean_jobs = (params[8] == "true");
        
        // 更新当前任务
        current_job_ = new_job;
        update_target_from_difficulty(current_difficulty_);
        
        // 触发任务回调
        std::lock_guard<std::mutex> lock(callback_mutex_);
        if (job_callback_) {
            job_callback_(current_job_);
        }
        
        return true;
    } catch (const std::exception& e) {
        handle_error("解析mining.notify失败: " + std::string(e.what()));
        return false;
    }
}

bool StratumClient::handle_mining_set_difficulty(const std::string& json) {
    try {
        std::vector<std::string> params = SimpleJSON::get_array(json, "params");
        if (params.empty()) return false;
        
        double new_difficulty = std::stod(params[0]);
        current_difficulty_ = new_difficulty;
        
        // 更新目标值
        update_target_from_difficulty(new_difficulty);
        
        // 触发难度回调
        std::lock_guard<std::mutex> lock(callback_mutex_);
        if (difficulty_callback_) {
            difficulty_callback_(new_difficulty);
        }
        
        return true;
    } catch (const std::exception& e) {
        handle_error("解析mining.set_difficulty失败: " + std::string(e.what()));
        return false;
    }
}

bool StratumClient::handle_response(const std::string& json) {
    try {
        // 检查是否有错误
        if (json.find("\"error\"") != std::string::npos && json.find("null") == std::string::npos) {
            std::string error_msg = SimpleJSON::get_string(json, "message");
            if (error_msg.empty()) {
                error_msg = "未知错误";
            }
            
            shares_rejected_++;
            handle_error("提交被拒绝: " + error_msg);
            return false;
        }
        
        // 成功响应
        if (json.find("\"result\"") != std::string::npos) {
            // 检查是否是订阅响应
            if (json.find("[[") != std::string::npos) {
                // 解析订阅响应
                std::vector<std::string> result = SimpleJSON::get_array(json, "result");
                if (result.size() >= 2) {
                    subscription_.subscription_id = result[0];
                    subscription_.extranonce1 = result[1];
                    if (result.size() >= 3) {
                        subscription_.extranonce2_size = std::stoi(result[2]);
                    }
                }
            } else {
                // 可能是share提交的成功响应
                shares_accepted_++;
            }
            return true;
        }
        
        return true;
    } catch (const std::exception& e) {
        handle_error("解析响应失败: " + std::string(e.what()));
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
    std::lock_guard<std::mutex> lock(callback_mutex_);
    if (error_callback_) {
        error_callback_(error);
    }
}

void StratumClient::handle_connection_lost() {
    if (connected_) {
        connected_ = false;
        close_socket();
        
        if (connect_callback_) {
            connect_callback_(false);
        }
    }
}

// StratumUtils实现
namespace StratumUtils {
    void difficulty_to_target(double difficulty, uint8_t* target) {
        // 比特币的最大目标值
        static const uint8_t max_target[32] = {
            0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
        };
        
        // 计算目标值 = max_target / difficulty
        memcpy(target, max_target, 32);
        
        // 简化的除法实现
        if (difficulty > 1.0) {
            uint64_t* target_64 = reinterpret_cast<uint64_t*>(target);
            target_64[0] = static_cast<uint64_t>(0x00000000FFFF0000ULL / difficulty);
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
    
    std::string calculate_merkle_root(const std::string& coinbase_hash, const std::vector<std::string>& merkle_branches) {
        std::string current_hash = coinbase_hash;
        
        for (const std::string& branch : merkle_branches) {
            // 简化的merkle计算，实际需要SHA256双重哈希
            current_hash = current_hash + branch;
        }
        
        return current_hash;
    }
    
    std::vector<uint8_t> build_block_header(const StratumJob& job, const std::string& extranonce1, 
                                           const std::string& extranonce2, uint32_t nonce) {
        std::vector<uint8_t> header(80);
        
        // 构建区块头（简化版本）
        // 实际实现需要正确的字节序和字段排列
        
        // 版本 (4字节)
        uint32_t version = std::stoul(job.version, nullptr, 16);
        memcpy(header.data(), &version, 4);
        
        // 前一个区块哈希 (32字节)
        std::vector<uint8_t> prev_hash = hex_decode(job.prev_block_hash);
        if (prev_hash.size() >= 32) {
            memcpy(header.data() + 4, prev_hash.data(), 32);
        }
        
        // Merkle根 (32字节) - 需要计算
        // 这里简化处理
        
        // 时间戳 (4字节)
        uint32_t timestamp = std::stoul(job.ntime, nullptr, 16);
        memcpy(header.data() + 68, &timestamp, 4);
        
        // 难度目标 (4字节)
        uint32_t bits = std::stoul(job.nbits, nullptr, 16);
        memcpy(header.data() + 72, &bits, 4);
        
        // Nonce (4字节)
        memcpy(header.data() + 76, &nonce, 4);
        
        return header;
    }
} 