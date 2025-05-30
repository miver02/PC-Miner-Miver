#include "mining_engine.h"
#include <iostream>
#include <iomanip>
#include <signal.h>
#include <thread>
#include <chrono>
#include <memory>
#include <atomic>

#ifdef _WIN32
    #include <windows.h>
    #include <conio.h>
#else
    #include <termios.h>
    #include <unistd.h>
    #include <fcntl.h>
#endif

// Global variables
std::unique_ptr<MiningEngine> g_mining_engine;
std::atomic<bool> g_running(true);

// Signal handler
void signal_handler(int signal) {
    std::cout << "\nReceived exit signal, stopping mining..." << std::endl;
    g_running = false;
    if (g_mining_engine) {
        g_mining_engine->stop_mining();
        g_mining_engine->disconnect_from_pool();
    }
}

// Cross-platform keyboard input detection
bool check_keyboard_hit() {
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

// Get keyboard input
char get_keyboard_char() {
#ifdef _WIN32
    return _getch();
#else
    return getchar();
#endif
}

// Clear screen
void clear_screen() {
#ifdef _WIN32
    system("cls");
#else
    system("clear");
#endif
}

// Show help information
void show_help() {
    std::cout << "PC Mining Program v1.0\n";
    std::cout << "Based on ESP32 NerdSoloMiner project\n\n";
    std::cout << "Usage:\n";
    std::cout << "  pcminer [options]\n\n";
    std::cout << "Options:\n";
    std::cout << "  -h, --help              Show help information\n";
    std::cout << "  -c, --config <file>     Specify config file\n";
    std::cout << "  -p, --pool <address>    Pool address\n";
    std::cout << "  -P, --port <port>       Pool port\n";
    std::cout << "  -u, --user <username>   Mining username\n";
    std::cout << "  -w, --password <pass>   Mining password\n";
    std::cout << "  -t, --threads <count>   Worker thread count\n";
    std::cout << "  -b, --batch <size>      Batch size\n";
    std::cout << "  --no-avx2              Disable AVX2 optimization\n";
    std::cout << "  --no-optimization      Disable all optimizations\n";
    std::cout << "  --benchmark            Run performance test\n\n";
    std::cout << "Interactive commands:\n";
    std::cout << "  s - Start/Stop mining\n";
    std::cout << "  p - Pause/Resume mining\n";
    std::cout << "  r - Reset statistics\n";
    std::cout << "  i - Show detailed info\n";
    std::cout << "  q - Quit program\n";
}

// Parse command line arguments
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
                std::cerr << "Error: " << arg << " requires argument" << std::endl;
                return false;
            }
        } else if (arg == "-P" || arg == "--port") {
            if (i + 1 < argc) {
                config.pool_port = std::stoi(argv[++i]);
            } else {
                std::cerr << "Error: " << arg << " requires argument" << std::endl;
                return false;
            }
        } else if (arg == "-u" || arg == "--user") {
            if (i + 1 < argc) {
                config.username = argv[++i];
            } else {
                std::cerr << "Error: " << arg << " requires argument" << std::endl;
                return false;
            }
        } else if (arg == "-w" || arg == "--password") {
            if (i + 1 < argc) {
                config.password = argv[++i];
            } else {
                std::cerr << "Error: " << arg << " requires argument" << std::endl;
                return false;
            }
        } else if (arg == "-t" || arg == "--threads") {
            if (i + 1 < argc) {
                config.thread_count = std::stoi(argv[++i]);
            } else {
                std::cerr << "Error: " << arg << " requires argument" << std::endl;
                return false;
            }
        } else if (arg == "-b" || arg == "--batch") {
            if (i + 1 < argc) {
                config.batch_size = std::stoul(argv[++i]);
            } else {
                std::cerr << "Error: " << arg << " requires argument" << std::endl;
                return false;
            }
        } else if (arg == "-c" || arg == "--config") {
            if (i + 1 < argc) {
                std::string config_file = argv[++i];
                if (!MiningUtils::load_config_from_file(config_file, config)) {
                    std::cerr << "Warning: Cannot load config file " << config_file << std::endl;
                }
            } else {
                std::cerr << "Error: " << arg << " requires argument" << std::endl;
                return false;
            }
        } else if (arg == "--no-avx2") {
            config.enable_avx2 = false;
        } else if (arg == "--no-optimization") {
            config.enable_optimization = false;
            config.enable_avx2 = false;
        } else {
            std::cerr << "Error: Unknown argument " << arg << std::endl;
            return false;
        }
    }
    
    return true;
}

// Show configuration
void show_config(const MiningConfig& config) {
    std::cout << "=== Mining Configuration ===" << std::endl;
    std::cout << "Pool address: " << config.pool_host << ":" << config.pool_port << std::endl;
    std::cout << "Username: " << config.username << std::endl;
    std::cout << "Worker threads: " << config.thread_count << std::endl;
    std::cout << "Batch size: " << config.batch_size << std::endl;
    std::cout << "AVX2 optimization: " << (config.enable_avx2 ? "Enabled" : "Disabled") << std::endl;
    std::cout << "Performance optimization: " << (config.enable_optimization ? "Enabled" : "Disabled") << std::endl;
    std::cout << "Auto reconnect: " << (config.auto_reconnect ? "Enabled" : "Disabled") << std::endl;
    std::cout << "============================" << std::endl;
}

// Show statistics
void show_stats(const MiningStats& stats) {
    auto now = std::chrono::steady_clock::now();
    auto runtime = std::chrono::duration_cast<std::chrono::seconds>(now - stats.start_time);
    
    std::cout << "\n=== Mining Statistics ===" << std::endl;
    std::cout << "Runtime: " << MiningUtils::format_duration(runtime) << std::endl;
    std::cout << "Total hashes: " << stats.total_hashes.load() << std::endl;
    std::cout << "Current hashrate: " << std::fixed << std::setprecision(2) 
              << stats.current_hashrate.load() / 1000.0 << " kH/s" << std::endl;
    std::cout << "Average hashrate: " << std::fixed << std::setprecision(2) 
              << stats.average_hashrate.load() / 1000.0 << " kH/s" << std::endl;
    std::cout << "Valid shares: " << stats.valid_shares.load() << std::endl;
    std::cout << "Invalid shares: " << stats.invalid_shares.load() << std::endl;
    if ((stats.valid_shares.load() + stats.invalid_shares.load()) > 0) {
        double acceptance_rate = (double)stats.valid_shares.load() / 
                               (stats.valid_shares.load() + stats.invalid_shares.load()) * 100.0;
        std::cout << "Acceptance rate: " << std::fixed << std::setprecision(1) << acceptance_rate << "%" << std::endl;
    }
    std::cout << "=========================" << std::endl;
}

// Show worker thread information
void show_worker_info(const std::vector<WorkerInfo*>& workers) {
    std::cout << "\n=== Worker Thread Info ===" << std::endl;
    for (size_t i = 0; i < workers.size(); i++) {
        const auto& worker = workers[i];
        std::cout << "Thread " << i << ": ";
        std::cout << std::fixed << std::setprecision(2) 
                  << worker->hashrate.load() / 1000.0 << " kH/s, ";
        std::cout << "Hashes: " << worker->hashes_done.load() << ", ";
        std::cout << "State: " << (worker->state.load() == WorkerState::WORKING ? "Working" : "Idle") << std::endl;
    }
    std::cout << "===========================" << std::endl;
}

// Run performance benchmark
void run_benchmark(MiningEngine& engine) {
    std::cout << "Running performance benchmark..." << std::endl;
    
    // Run SHA256 benchmark
    std::cout << "\n=== SHA256 Benchmark ===" << std::endl;
    SHA256Core::benchmark_sha256();
    
    // Run batch processing benchmark
    std::cout << "\n=== Batch Processing Benchmark ===" << std::endl;
    SHA256Core::benchmark_batch_processing();
    
    // Run mining engine benchmark
    std::cout << "\n=== Mining Engine Benchmark ===" << std::endl;
    engine.benchmark_performance();
    
    std::cout << "\nBenchmark completed." << std::endl;
}

// Main loop
void main_loop(MiningEngine& engine) {
    bool mining_started = false;
    bool mining_paused = false;
    
    // Set callback functions
    engine.set_share_found_callback([](const StratumShare& share, bool accepted) {
        std::cout << "\nShare " << (accepted ? "accepted" : "rejected") 
                  << " - Job ID: " << share.job_id 
                  << ", Nonce: 0x" << std::hex << share.nonce << std::dec << std::endl;
    });
    
    engine.set_status_callback([](const std::string& message) {
        std::cout << "\nStatus: " << message << std::endl;
    });
    
    std::cout << "\n=== PC Mining Program ===" << std::endl;
    std::cout << "Commands: 's' = start/stop, 'p' = pause/resume, 'r' = reset stats, 'i' = show stats, 'w' = workers, 'h' = help, 'q' = quit" << std::endl;
    
    // Connect to pool
    std::cout << "Connecting to pool..." << std::endl;
    if (!engine.connect_to_pool()) {
        std::cout << "Failed to connect to pool!" << std::endl;
        return;
    }
    
    std::cout << "Connected to pool successfully!" << std::endl;
    std::cout << "Waiting for jobs..." << std::endl;
    
    // Wait a bit for initial job
    std::this_thread::sleep_for(std::chrono::seconds(2));
    
    // Start mining automatically
    std::cout << "Starting mining..." << std::endl;
    if (engine.start_mining()) {
        mining_started = true;
        std::cout << "Mining started!" << std::endl;
    } else {
        std::cout << "Failed to start mining!" << std::endl;
    }
    
    // Set up automatic stats display
    auto last_stats_display = std::chrono::steady_clock::now();
    const auto stats_display_interval = std::chrono::seconds(30); // 每30秒显示一次算力
    
    std::cout << "\nPress 'i' to show stats, 'w' to show workers, or any other key for commands..." << std::endl;
    
    // Main command loop
    while (g_running) {
        // Check for keyboard input (non-blocking)
        if (check_keyboard_hit()) {
            char cmd = get_keyboard_char();
            
            switch (cmd) {
                case 's':
                case 'S':
                    if (!mining_started) {
                        std::cout << "Starting mining..." << std::endl;
                        if (engine.start_mining()) {
                            mining_started = true;
                            mining_paused = false;
                            std::cout << "Mining started!" << std::endl;
                        } else {
                            std::cout << "Failed to start mining!" << std::endl;
                        }
                    } else {
                        std::cout << "Stopping mining..." << std::endl;
                        engine.stop_mining();
                        mining_started = false;
                        mining_paused = false;
                        std::cout << "Mining stopped!" << std::endl;
                    }
                    break;
                    
                case 'p':
                case 'P':
                    if (mining_started) {
                        if (!mining_paused) {
                            std::cout << "Pausing mining..." << std::endl;
                            engine.pause_mining();
                            mining_paused = true;
                            std::cout << "Mining paused!" << std::endl;
                        } else {
                            std::cout << "Resuming mining..." << std::endl;
                            engine.resume_mining();
                            mining_paused = false;
                            std::cout << "Mining resumed!" << std::endl;
                        }
                    } else {
                        std::cout << "Mining is not started!" << std::endl;
                    }
                    break;
                    
                case 'i':
                case 'I':
                    show_stats(engine.get_stats());
                    break;
                    
                case 'w':
                case 'W':
                    show_worker_info(engine.get_worker_info());
                    break;
                    
                case 'r':
                case 'R':
                    std::cout << "Resetting statistics..." << std::endl;
                    engine.reset_stats();
                    std::cout << "Statistics reset!" << std::endl;
                    break;
                    
                case 'q':
                case 'Q':
                    std::cout << "Shutting down..." << std::endl;
                    g_running = false;
                    break;
                    
                case 'h':
                case 'H':
                    std::cout << "\n=== Help - Available Commands ===" << std::endl;
                    std::cout << "s/S - Start/Stop mining" << std::endl;
                    std::cout << "p/P - Pause/Resume mining" << std::endl;
                    std::cout << "r/R - Reset statistics" << std::endl;
                    std::cout << "i/I - Show detailed statistics" << std::endl;
                    std::cout << "w/W - Show worker thread info" << std::endl;
                    std::cout << "h/H - Show this help" << std::endl;
                    std::cout << "q/Q - Quit program" << std::endl;
                    std::cout << "=================================" << std::endl;
                    break;
                    
                default:
                    std::cout << "Commands: 's' = start/stop, 'p' = pause/resume, 'r' = reset stats, 'i' = show stats, 'w' = workers, 'h' = help, 'q' = quit" << std::endl;
                    break;
            }
        }
        
        // Check if it's time to display stats automatically
        auto now = std::chrono::steady_clock::now();
        if (mining_started && !mining_paused && 
            std::chrono::duration_cast<std::chrono::seconds>(now - last_stats_display) >= stats_display_interval) {
            
            std::cout << "\n=== Auto Stats Update ===" << std::endl;
            show_stats(engine.get_stats());
            last_stats_display = now;
        }
        
        // Small sleep to avoid high CPU usage
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    // Cleanup
    if (mining_started) {
        std::cout << "Stopping mining..." << std::endl;
        engine.stop_mining();
    }
}

int main(int argc, char* argv[]) {
    // Set signal handlers
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    // Default configuration
    MiningConfig config;
    bool show_help_flag = false;
    bool benchmark_flag = false;
    
    // Parse command line arguments
    if (!parse_arguments(argc, argv, config, show_help_flag, benchmark_flag)) {
        return 1;
    }
    
    if (show_help_flag) {
        show_help();
        return 0;
    }
    
    try {
        // Create mining engine
        g_mining_engine = std::make_unique<MiningEngine>();
        g_mining_engine->set_config(config);
        
        if (benchmark_flag) {
            run_benchmark(*g_mining_engine);
            return 0;
        }
        
        // Show configuration
        show_config(config);
        
        // Enter main loop
        main_loop(*g_mining_engine);
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    std::cout << "Program exited." << std::endl;
    return 0;
} 