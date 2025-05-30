# 快速入门指南

本指南将帮助您快速编译和运行PC挖矿程序。

## 🚀 5分钟快速开始

### Windows用户

1. **安装依赖**
   ```bash
   # 下载并安装Visual Studio Community 2019/2022
   # https://visualstudio.microsoft.com/downloads/
   
   # 下载并安装CMake
   # https://cmake.org/download/
   ```

2. **编译程序**
   ```bash
   # 双击运行构建脚本
   build.bat
   ```

3. **运行测试**
   ```bash
   # 运行性能测试
   pcminer.exe --benchmark
   ```

4. **开始挖矿**
   ```bash
   # 连接到矿池（请替换为真实的矿池信息） ./pcminer.exe -p stratum+tcp://public-pool.io -P 21496 -u 12yYM9Mbr4rrRSeptxsr7PVZ3G3gM9MFt6
   pcminer.exe -p stratum.slushpool.com -P 3333 -u your_username.worker
   ```

### Linux用户

1. **安装依赖**
   ```bash
   # Ubuntu/Debian
   sudo apt-get update
   sudo apt-get install build-essential cmake libssl-dev
   
   # CentOS/RHEL
   sudo yum groupinstall 'Development Tools'
   sudo yum install cmake openssl-devel
   ```

2. **编译程序**
   ```bash
   # 运行构建脚本
   ./build.sh
   ```

3. **运行测试**
   ```bash
   # 运行性能测试
   ./build/pcminer --benchmark
   ```

4. **开始挖矿**
   ```bash
   # 连接到矿池
   ./build/pcminer -p stratum.slushpool.com -P 3333 -u your_username.worker
   ```

## 📊 性能优化建议

### 基本配置
- **线程数**: 设置为CPU核心数
- **批处理大小**: 1,000,000 (可根据内存调整)
- **启用优化**: 确保启用AVX2和其他优化选项

### 示例配置
```bash
# 8核CPU的推荐配置
pcminer -p pool.com -P 3333 -u user -t 8 -b 1000000
```

### 高级优化
1. **关闭不必要的程序**释放CPU资源
2. **设置高性能电源模式**
3. **在BIOS中禁用CPU节能功能**
4. **确保良好的散热**

## 🔧 常见问题

### Q: 编译失败怎么办？
A: 检查以下项目：
- 确保安装了所有依赖项
- 检查编译器版本是否支持C++17
- 确保OpenSSL开发库已正确安装

### Q: 连接矿池失败？
A: 检查以下项目：
- 网络连接是否正常
- 矿池地址和端口是否正确
- 用户名格式是否正确
- 防火墙是否阻止了连接

### Q: 算力很低怎么办？
A: 尝试以下优化：
- 增加线程数到CPU核心数
- 调整批处理大小
- 启用AVX2优化
- 关闭其他占用CPU的程序

### Q: 份额被拒绝？
A: 可能的原因：
- 用户名格式不正确
- 网络延迟过高
- 系统时间不准确

## 📈 监控和统计

### 实时监控
程序运行时会显示：
- 当前算力 (H/s)
- 总哈希数
- 有效/无效份额
- 接受率

### 交互命令
- `s` - 开始/停止挖矿
- `p` - 暂停/恢复
- `r` - 重置统计
- `i` - 显示详细信息
- `h` - 显示帮助
- `q` - 退出程序

## 🎯 推荐矿池

### 主流矿池
1. **Slush Pool** (推荐新手)
   - 地址: `stratum.slushpool.com:3333`
   - 特点: 历史悠久，稳定可靠

2. **F2Pool**
   - 地址: `btc.f2pool.com:3333`
   - 特点: 全球最大矿池之一

3. **Antpool**
   - 地址: `stratum.antpool.com:3333`
   - 特点: 比特大陆旗下矿池

### 配置示例
```bash
# Slush Pool
pcminer -p stratum.slushpool.com -P 3333 -u username.worker001

# F2Pool  
pcminer -p btc.f2pool.com -P 3333 -u username.001

# 使用配置文件
cp miner.conf.example miner.conf
# 编辑miner.conf文件
pcminer -c miner.conf
```

## ⚠️ 重要提醒

1. **教育目的**: 本程序主要用于学习和研究
2. **收益预期**: CPU挖矿无法与ASIC设备竞争
3. **电力成本**: 注意电力消耗和成本
4. **法律合规**: 遵守当地法律法规
5. **硬件保护**: 注意散热，避免硬件损坏

## 📞 获取帮助

- 查看完整文档: `README.md`
- 运行帮助命令: `pcminer --help`
- 查看示例配置: `miner.conf.example`

---

**开始您的挖矿之旅！** 🚀 