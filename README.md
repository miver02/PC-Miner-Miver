# PC挖矿程序 v1.0

基于ESP32 NerdSoloMiner项目的PC版本，专为普通计算机设计的高性能比特币挖矿程序。

## 特性

- 🚀 **高性能优化**: 支持AVX2、SSE2等CPU指令集优化
- 🔄 **批处理挖矿**: 高效的批量哈希计算
- 🧵 **多线程支持**: 充分利用多核CPU性能
- 🌐 **Stratum协议**: 完整的矿池通信支持
- 📊 **实时统计**: 详细的挖矿统计和监控
- ⚙️ **灵活配置**: 支持命令行参数和配置文件
- 🔄 **自动重连**: 网络断线自动重连功能
- 🎯 **跨平台**: 支持Windows和Linux系统

## 系统要求

- **操作系统**: Windows 10+ 或 Linux (Ubuntu 18.04+)
- **编译器**: GCC 7+ 或 MSVC 2019+
- **CPU**: 支持SSE2指令集（推荐支持AVX2）
- **内存**: 最少512MB可用内存
- **网络**: 稳定的互联网连接

## 编译安装

### 依赖项

- CMake 3.12+
- OpenSSL开发库
- pthread库（Linux）

### Windows编译

```bash
# 安装依赖（使用vcpkg）
vcpkg install openssl:x64-windows

# 编译
mkdir build
cd build
cmake .. -DCMAKE_TOOLCHAIN_FILE=[vcpkg root]/scripts/buildsystems/vcpkg.cmake
cmake --build . --config Release
```

### Linux编译

```bash
# 安装依赖
sudo apt-get update
sudo apt-get install build-essential cmake libssl-dev

# 编译
mkdir build
cd build
cmake ..
make -j$(nproc)
```

## 使用方法

### 基本用法

```bash
# 基本挖矿命令  
./pcminer -p stratum+tcp://pool.example.com -P 3333 -u your_username

# 指定线程数和批处理大小
./pcminer -p pool.example.com -P 3333 -u username -t 8 -b 1000000

# 使用配置文件
./pcminer -c miner.conf
```

### 命令行参数

| 参数 | 说明 | 示例 |
|------|------|------|
| `-h, --help` | 显示帮助信息 | `--help` |
| `-p, --pool` | 矿池地址 | `-p pool.example.com` |
| `-P, --port` | 矿池端口 | `-P 3333` |
| `-u, --user` | 挖矿用户名 | `-u your_username` |
| `-w, --password` | 挖矿密码 | `-w x` |
| `-t, --threads` | 工作线程数 | `-t 8` |
| `-b, --batch` | 批处理大小 | `-b 1000000` |
| `-c, --config` | 配置文件路径 | `-c miner.conf` |
| `--no-avx2` | 禁用AVX2优化 | `--no-avx2` |
| `--no-optimization` | 禁用所有优化 | `--no-optimization` |
| `--benchmark` | 运行性能测试 | `--benchmark` |

### 交互式命令

程序运行时支持以下键盘命令：

| 按键 | 功能 |
|------|------|
| `s` | 开始/停止挖矿 |
| `p` | 暂停/恢复挖矿 |
| `r` | 重置统计信息 |
| `i` | 显示详细信息 |
| `h` | 显示帮助 |
| `q` | 退出程序 |

### 配置文件

创建 `miner.conf` 文件：

```ini
pool_host=stratum+tcp://pool.example.com
pool_port=3333
username=your_username
password=x
thread_count=8
batch_size=1000000
enable_avx2=true
enable_optimization=true
```

## 性能优化

### CPU优化

1. **启用AVX2**: 如果CPU支持，确保启用AVX2优化
2. **线程数配置**: 通常设置为CPU核心数
3. **批处理大小**: 根据CPU缓存大小调整（推荐100万-1000万）

### 系统优化

1. **关闭不必要程序**: 释放CPU和内存资源
2. **设置高性能模式**: Windows电源管理设置为高性能
3. **禁用CPU节能**: 在BIOS中禁用CPU节能功能

## 矿池配置

### 推荐矿池

- **Slush Pool**: `stratum+tcp://stratum.slushpool.com:3333`
- **F2Pool**: `stratum+tcp://btc.f2pool.com:3333`
- **Antpool**: `stratum+tcp://stratum.antpool.com:3333`
- **ViaBTC**: `stratum+tcp://btc.viabtc.com:3333`

### 配置示例

```bash
# Slush Pool
./pcminer -p stratum.slushpool.com -P 3333 -u username.worker

# F2Pool
./pcminer -p btc.f2pool.com -P 3333 -u username.worker

# 本地测试（如果有本地节点）
./pcminer -p localhost -P 3333 -u test
```

## 监控和统计

### 实时显示

程序运行时会显示：
- 当前算力 (H/s, KH/s, MH/s)
- 总哈希数
- 有效/无效份额
- 接受率
- 运行时间

### 详细信息

按 `i` 键查看：
- 挖矿配置
- 详细统计信息
- 工作线程状态
- 每个线程的算力

## 故障排除

### 常见问题

1. **连接失败**
   - 检查网络连接
   - 验证矿池地址和端口
   - 确认防火墙设置

2. **算力过低**
   - 检查CPU使用率
   - 调整线程数和批处理大小
   - 启用CPU优化选项

3. **份额被拒绝**
   - 检查用户名格式
   - 验证矿池难度设置
   - 确认时间同步

### 调试模式

```bash
# 运行性能测试
./pcminer --benchmark

# 禁用优化进行测试
./pcminer --no-optimization -p pool.com -P 3333 -u user
```

## 开发信息

### 项目结构

```
new/
├── CMakeLists.txt          # 构建配置
├── README.md              # 说明文档
├── include/               # 头文件
│   ├── sha256_core.h      # SHA256算法
│   ├── stratum_client.h   # Stratum客户端
│   └── mining_engine.h    # 挖矿引擎
└── src/                   # 源代码
    ├── main.cpp           # 主程序
    ├── sha256/            # SHA256实现
    │   ├── sha256_core.cpp
    │   └── sha256_batch.cpp
    ├── network/           # 网络通信
    │   └── stratum_client.cpp
    └── mining/            # 挖矿逻辑
        └── mining_engine.cpp
```

### 算法实现

- **SHA256核心**: 优化的SHA256双重哈希实现
- **批处理**: 高效的批量哈希计算
- **AVX2优化**: 利用SIMD指令加速
- **多线程**: 无锁的工作分配机制

## 许可证

本项目基于MIT许可证开源，详见LICENSE文件。

## 贡献

欢迎提交Issue和Pull Request来改进项目。

## 免责声明

本软件仅供学习和研究使用。挖矿可能消耗大量电力和计算资源，请根据当地法律法规和电力成本合理使用。作者不对使用本软件造成的任何损失承担责任。

## 联系方式

如有问题或建议，请通过GitHub Issues联系。

---

**注意**: 比特币挖矿需要专业的ASIC设备才能获得实际收益。本程序主要用于学习SHA256算法和挖矿协议的实现原理。 