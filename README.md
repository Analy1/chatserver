# C++ 高性能聊天服务器集群

基于 C++11 开发的高并发聊天服务器，支持集群部署、跨服务器消息互通、海量数据持久化，并自研异步日志系统。

## 技术栈

| 类别     | 技术                       |
| :------- | :------------------------- |
| 编程语言 | C++11                      |
| 网络框架 | Muduo（主从 Reactor 模式） |
| 负载均衡 | Nginx TCP 反向代理         |
| 数据存储 | MySQL + 连接池             |
| 跨服通信 | Redis 发布/订阅            |
| 序列化   | JSON（nlohmann/json）      |
| 构建工具 | CMake                      |
| 日志系统 | 异步日志                   |

## 核心功能

- 用户注册/登录/注销
- 一对一即时聊天
- 群组聊天（创建、加入、群发）
- 离线消息存储与自动推送
- 好友管理（添加好友）
- TCP 心跳保活与超时断开
- 集群化水平扩展

## 项目亮点

### 1. 集群化架构

- Nginx TCP 负载均衡将客户端连接动态分发至后端服务器集群
- Redis 发布/订阅实现跨服务器消息转发，解决集群环境下用户跨节点通信难题

### 2. 高性能数据库访问

- **手写 MySQL 连接池**，复用连接资源，消除频繁创建/销毁开销
- 高并发场景下 QPS 提升 10 倍，避免连接风暴

### 3. 异步日志系统

- **8 层架构**：Timestamp → LogMessage → Logger → AsynLogging → LogFile → AppendFile → 磁盘
- **异步非阻塞**：生产者-消费者模式，业务线程仅需微秒级内存拷贝，磁盘 I/O 由后台单线程批量执行
- **双缓冲区交换**：`std::swap` 零拷贝转移数据，最小化锁持有时间
- **自动滚动**：支持按文件大小（默认 100MB）和按天滚动，文件名含微秒时间戳、主机名、进程 ID
- **分级输出**：TRACE / DEBUG / INFO / WARN / ERROR / FATAL 六级日志，运行时动态可调
- **线程安全**：互斥锁 + 条件变量 + CountDownLatch 门闩同步
- **优雅停止**：信号处理机制，确保进程退出时缓冲区全部落盘，日志零丢失

### 4. 健壮性设计

- TCP 心跳保活 + 超时断开，防止连接表膨胀
- 基于长度字段的粘包拆包方案，保证消息完整性
- JSON 私有通信协议，消息 ID 驱动业务分发，高度解耦



## 目录结构

chatserver/
├── include/
│ ├── asynlog/ # 日志系统头文件
│ │ ├── Timestamp.hpp
│ │ ├── LogMessage.hpp
│ │ ├── Logger.hpp
│ │ ├── AppendFile.hpp
│ │ ├── LogFile.hpp
│ │ ├── CountDownLatch.hpp
│ │ ├── AsynLogging.hpp
│ │ └── LogCommon.hpp
│ └── server/ # 业务模块头文件
│ ├── db/ # 数据库连接池
│ ├── model/ # 数据模型
│ └── redis/ # Redis 模块
├── src/
│ ├── asynlog/ # 日志系统源文件
│ └── server/ # 业务模块源文件
├── CMakeLists.txt
└── README.md



## 日志系统使用示例

```cpp
#include "AsynLogging.hpp"
#include "Logger.hpp"

// 全局异步日志对象
tulun::AsynLogging g_log("chatserver", 100*1024*1024, 3);

int main() {
    // 启动异步日志
    g_log.start();
    tulun::Logger::setOutput([](const std::string& msg) { g_log.append(msg); });
    tulun::Logger::setFlush ([]() { g_log.flush(); });
    tulun::Logger::setLogLevel(tulun::LOG_LEVEL::INFO);

    LOG_INFO << "Server started";
    LOG_ERROR << "Something went wrong!";
    
    // 退出时停止
    g_log.stop();
}
```



