#include "mining_engine.h"
#include <iostream>
#include <iomanip>
#include <signal.h>
#include <thread>
#include <chrono>

#ifdef _WIN32
    #include <windows.h>
    #include <conio.h>
#else
    #include <termios.h>
    #include <unistd.h>
#endif

// 全局变量
std::unique_ptr<MiningEngine> g_mining_engine;
std::atomic<bool> g_running(true);

// 信号处理函数
void signal_handler(int signal) {
    std::cout << "\n收到退出信号，正在停止挖矿..." << std::endl;
    g_running = false;
    if (g_mining_engine) {
        g_mining_engine->stop_mining();
        g_mining_engine->disconnect_from_pool();
    }
}

// 跨平台的键盘输入检测
bool kbhit() {
#ifdef _WIN32
    return _kbhit();
#else
    struct termios oldt, newt;
    int ch;
    int oldf;
    
    tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt;
    newt.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);
    oldf = fcntl(STDIN_FILENO, F_GETFL, 0);
    fcntl(STDIN_FILENO, F_SETFL, oldf | O_NONBLOCK);
    
    ch = getchar();
    
    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
    fcntl(STDIN_FILENO, F_SETFL, oldf);
    
    if(ch != EOF) {
        ungetc(ch, stdin);
        return true;
    }
    
    return false;
#endif
}

// 获取键盘输入
char getch() {
#ifdef _WIN32
    return _getch();
#else
    return getchar();
#endif
}

// 清屏函数
void clear_screen() {
#ifdef _WIN32
    system("cls");
#else
    system("clear");
#endif
}

// 显示帮助信息
void show_help() {
    std::cout << "PC挖矿程序 v1.0\n";
    std::cout << "基于ESP32 NerdSoloMiner项目的PC版本\n\n";
    std::cout << "使用方法:\n";
    std::cout << "  pcminer [选项]\n\n";
    std::cout << "选项:\n";
    std::cout << "  -h, --help              显示帮助信息\n";
    std::cout << "  -c, --config <文件>     指定配置文件\n";
    std::cout << "  -p, --pool <地址>       矿池地址\n";
    std::cout << "  -P, --port <端口>       矿池端口\n";
    std::cout << "  -u, --user <用户名>     挖矿用户名\n";
    std::cout << "  -w, --password <密码>   挖矿密码\n";
    std::cout << "  -t, --threads <数量>    工作线程数\n";
    std::cout << "  -b, --batch <大小>      批处理大小\n";
    std::cout << "  --no-avx2              禁用AVX2优化\n";
    std::cout << "  --no-optimization      禁用所有优化\n";
    std::cout << "  --benchmark            运行性能测试\n\n";
    std::cout << "交互式命令:\n";
    std::cout << "  s - 开始/停止挖矿\n";
    std::cout << "  p - 暂停/恢复挖矿\n";
    std::cout << "  r - 重置统计信息\n";
    std::cout << "  i - 显示详细信息\n";
    std::cout << "  q - 退出程序\n";
}

// 解析命令行参数
bool parse_arguments(int argc, char* argv[], MiningConfig& config, bool& show_help_flag, bool& benchmark_flag) {
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        
        if (arg == "-h" || arg == "--help") {
            show_help_flag = true;
            return true;
        } else if (arg == "--benchmark") {
            benchmark_flag = true;
        } else if (arg == "-p" || arg == "--pool") {
            if (i + 1 < argc) {
                config.pool_host = argv[++i];
            } else {
                std::cerr << "错误: " << arg << " 需要参数" << std::endl;
                return false;
            }
        } else if (arg == "-P" || arg == "--port") {
            if (i + 1 < argc) {
                config.pool_port = std::stoi(argv[++i]);
            } else {
                std::cerr << "错误: " << arg << " 需要参数" << std::endl;
                return false;
            }
        } else if (arg == "-u" || arg == "--user") {
            if (i + 1 < argc) {
                config.username = argv[++i];
            } else {
                std::cerr << "错误: " << arg << " 需要参数" << std::endl;
                return false;
            }
        } else if (arg == "-w" || arg == "--password") {
            if (i + 1 < argc) {
                config.password = argv[++i];
            } else {
                std::cerr << "错误: " << arg << " 需要参数" << std::endl;
                return false;
            }
        } else if (arg == "-t" || arg == "--threads") {
            if (i + 1 < argc) {
                config.thread_count = std::stoi(argv[++i]);
            } else {
                std::cerr << "错误: " << arg << " 需要参数" << std::endl;
                return false;
            }
        } else if (arg == "-b" || arg == "--batch") {
            if (i + 1 < argc) {
                config.batch_size = std::stoul(argv[++i]);
            } else {
                std::cerr << "错误: " << arg << " 需要参数" << std::endl;
                return false;
            }
        } else if (arg == "-c" || arg == "--config") {
            if (i + 1 < argc) {
                std::string config_file = argv[++i];
                if (!MiningUtils::load_config_from_file(config_file, config)) {
                    std::cerr << "警告: 无法加载配置文件 " << config_file << std::endl;
                }
            } else {
                std::cerr << "错误: " << arg << " 需要参数" << std::endl;
                return false;
            }
        } else if (arg == "--no-avx2") {
            config.enable_avx2 = false;
        } else if (arg == "--no-optimization") {
            config.enable_optimization = false;
            config.enable_avx2 = false;
        } else {
            std::cerr << "错误: 未知参数 " << arg << std::endl;
            return false;
        }
    }
    
    return true;
}

// 显示配置信息
void show_config(const MiningConfig& config) {
    std::cout << "=== 挖矿配置 ===" << std::endl;
    std::cout << "矿池地址: " << config.pool_host << ":" << config.pool_port << std::endl;
    std::cout << "用户名: " << config.username << std::endl;
    std::cout << "工作线程: " << config.thread_count << std::endl;
    std::cout << "批处理大小: " << config.batch_size << std::endl;
    std::cout << "AVX2优化: " << (config.enable_avx2 ? "启用" : "禁用") << std::endl;
    std::cout << "性能优化: " << (config.enable_optimization ? "启用" : "禁用") << std::endl;
    std::cout << "自动重连: " << (config.auto_reconnect ? "启用" : "禁用") << std::endl;
    std::cout << "==================" << std::endl;
}

// 显示统计信息
void show_stats(const MiningStats& stats) {
    auto now = std::chrono::steady_clock::now();
    auto runtime = std::chrono::duration_cast<std::chrono::seconds>(now - stats.start_time);
    
    std::cout << "\n=== 挖矿统计 ===" << std::endl;
    std::cout << "运行时间: " << MiningUtils::format_duration(runtime) << std::endl;
    std::cout << "总哈希数: " << stats.total_hashes.load() << std::endl;
    std::cout << "当前算力: " << MiningUtils::format_hashrate(stats.current_hashrate.load()) << std::endl;
    std::cout << "平均算力: " << MiningUtils::format_hashrate(stats.average_hashrate.load()) << std::endl;
    std::cout << "有效份额: " << stats.valid_shares.load() << std::endl;
    std::cout << "无效份额: " << stats.invalid_shares.load() << std::endl;
    
    uint64_t total_shares = stats.valid_shares.load() + stats.invalid_shares.load();
    if (total_shares > 0) {
        double acceptance_rate = (double)stats.valid_shares.load() / total_shares * 100.0;
        std::cout << "接受率: " << std::fixed << std::setprecision(2) << acceptance_rate << "%" << std::endl;
    }
    std::cout << "=================" << std::endl;
}

// 显示工作线程信息
void show_worker_info(const std::vector<WorkerInfo*>& workers) {
    std::cout << "\n=== 工作线程状态 ===" << std::endl;
    std::cout << "ID\t状态\t\t哈希数\t\t算力" << std::endl;
    std::cout << "----------------------------------------" << std::endl;
    
    for (const auto& worker : workers) {
        std::string state_str;
        switch (worker->state.load()) {
            case WorkerState::IDLE: state_str = "空闲"; break;
            case WorkerState::WORKING: state_str = "工作中"; break;
            case WorkerState::PAUSED: state_str = "暂停"; break;
            case WorkerState::STOPPED: state_str = "停止"; break;
        }
        
        std::cout << worker->worker_id << "\t" 
                  << state_str << "\t\t"
                  << worker->hashes_done.load() << "\t\t"
                  << MiningUtils::format_hashrate(worker->hashrate.load()) << std::endl;
    }
    std::cout << "===================" << std::endl;
}

// 运行性能测试
void run_benchmark(MiningEngine& engine) {
    std::cout << "开始性能测试..." << std::endl;
    
    // 测试不同的配置
    std::vector<std::pair<std::string, bool>> test_configs = {
        {"基础算法", false},
        {"优化算法", true}
    };
    
    for (const auto& test : test_configs) {
        std::cout << "\n测试 " << test.first << ":" << std::endl;
        
        MiningConfig config = engine.get_config();
        config.enable_optimization = test.second;
        engine.set_config(config);
        
        double hashrate = engine.benchmark_performance(10);
        std::cout << "结果: " << MiningUtils::format_hashrate(hashrate) << std::endl;
    }
}

// 主循环
void main_loop(MiningEngine& engine) {
    bool mining_started = false;
    
    // 设置回调函数
    engine.set_stats_callback([](const MiningStats& stats) {
        // 定期显示统计信息（在状态栏）
        std::cout << "\r算力: " << MiningUtils::format_hashrate(stats.current_hashrate.load())
                  << " | 份额: " << stats.valid_shares.load() << "/" << stats.invalid_shares.load()
                  << " | 哈希: " << stats.total_hashes.load() << std::flush;
    });
    
    engine.set_share_found_callback([](const StratumShare& share, bool accepted) {
        std::cout << "\n" << (accepted ? "✓" : "✗") << " 份额 " 
                  << (accepted ? "已接受" : "被拒绝") << " - Nonce: 0x" 
                  << std::hex << share.nonce << std::dec << std::endl;
    });
    
    engine.set_error_callback([](const std::string& error) {
        std::cout << "\n错误: " << error << std::endl;
    });
    
    engine.set_status_callback([](const std::string& status) {
        std::cout << "\n状态: " << status << std::endl;
    });
    
    std::cout << "\n=== PC挖矿程序 ===" << std::endl;
    std::cout << "按 's' 开始挖矿, 'q' 退出, 'h' 查看帮助" << std::endl;
    
    while (g_running) {
        if (kbhit()) {
            char key = getch();
            
            switch (key) {
                case 's':
                case 'S':
                    if (!mining_started) {
                        if (engine.connect_to_pool()) {
                            if (engine.start_mining()) {
                                std::cout << "\n挖矿已开始" << std::endl;
                                mining_started = true;
                            }
                        }
                    } else {
                        engine.stop_mining();
                        std::cout << "\n挖矿已停止" << std::endl;
                        mining_started = false;
                    }
                    break;
                    
                case 'p':
                case 'P':
                    if (mining_started) {
                        if (engine.is_mining()) {
                            engine.pause_mining();
                            std::cout << "\n挖矿已暂停" << std::endl;
                        } else {
                            engine.resume_mining();
                            std::cout << "\n挖矿已恢复" << std::endl;
                        }
                    }
                    break;
                    
                case 'r':
                case 'R':
                    engine.reset_stats();
                    std::cout << "\n统计信息已重置" << std::endl;
                    break;
                    
                case 'i':
                case 'I':
                    clear_screen();
                    show_config(engine.get_config());
                    show_stats(engine.get_stats());
                    show_worker_info(engine.get_worker_info());
                    break;
                    
                case 'h':
                case 'H':
                    clear_screen();
                    show_help();
                    break;
                    
                case 'q':
                case 'Q':
                    g_running = false;
                    break;
                    
                default:
                    break;
            }
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

// 主函数
int main(int argc, char* argv[]) {
    // 设置信号处理
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    // 创建默认配置
    MiningConfig config;
    bool show_help_flag = false;
    bool benchmark_flag = false;
    
    // 解析命令行参数
    if (!parse_arguments(argc, argv, config, show_help_flag, benchmark_flag)) {
        return 1;
    }
    
    if (show_help_flag) {
        show_help();
        return 0;
    }
    
    // 创建挖矿引擎
    g_mining_engine = std::make_unique<MiningEngine>();
    g_mining_engine->set_config(config);
    
    // 显示系统信息
    std::cout << "PC挖矿程序启动中..." << std::endl;
    std::cout << "CPU核心数: " << std::thread::hardware_concurrency() << std::endl;
    std::cout << "AVX2支持: " << (MiningUtils::has_avx2_support() ? "是" : "否") << std::endl;
    std::cout << "SSE2支持: " << (MiningUtils::has_sse2_support() ? "是" : "否") << std::endl;
    
    // 验证配置
    if (config.pool_host.empty() || config.username.empty()) {
        std::cerr << "错误: 必须指定矿池地址和用户名" << std::endl;
        std::cerr << "使用 --help 查看帮助信息" << std::endl;
        return 1;
    }
    
    // 运行性能测试
    if (benchmark_flag) {
        run_benchmark(*g_mining_engine);
        return 0;
    }
    
    // 显示配置
    show_config(config);
    
    // 保存配置到文件
    MiningUtils::save_config_to_file("miner.conf", config);
    
    try {
        // 运行主循环
        main_loop(*g_mining_engine);
    } catch (const std::exception& e) {
        std::cerr << "程序异常: " << e.what() << std::endl;
        return 1;
    }
    
    std::cout << "\n程序正常退出" << std::endl;
    return 0;
} 