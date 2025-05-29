cmake_minimum_required(VERSION 3.16)
project(PCMiner VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 编译器优化设置
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    if(MSVC)
        set(CMAKE_CXX_FLAGS_RELEASE "/O2 /DNDEBUG /arch:AVX2")
    else()
        set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG -march=native -mtune=native -ffast-math -funroll-loops -finline-functions -fomit-frame-pointer")
    endif()
endif()

# 查找依赖包
find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED)

# 包含目录
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# 源文件
set(SOURCES
    src/main.cpp
    src/sha256/sha256_core.cpp
    src/sha256/sha256_batch.cpp
    src/network/stratum_client.cpp
    src/mining/mining_engine.cpp
)

# 创建可执行文件
add_executable(pcminer ${SOURCES})

# 设置可执行文件名
set_target_properties(pcminer PROPERTIES OUTPUT_NAME "pcminer")

# 链接库
target_link_libraries(pcminer 
    Threads::Threads
    OpenSSL::SSL
    OpenSSL::Crypto
)

# Windows特定设置
if(WIN32)
    target_link_libraries(pcminer ws2_32)
    # 设置控制台应用程序
    set_target_properties(pcminer PROPERTIES
        WIN32_EXECUTABLE FALSE
    )
endif()

# 设置输出目录
set_target_properties(pcminer PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/Debug
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/Release
)

# 安装规则
install(TARGETS pcminer
    RUNTIME DESTINATION bin
)

# 复制配置文件示例
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/miner.conf.example")
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/miner.conf.example" 
        "${CMAKE_BINARY_DIR}/miner.conf.example" 
        COPYONLY
    )
endif() 