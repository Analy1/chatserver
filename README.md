# C++ 高性能分布式聊天服务器集群

[![C++11](https://img.shields.io/badge/C%2B%2B-11/17-blue)](https://en.cppreference.com/w/cpp/17)
[![CMake](https://img.shields.io/badge/CMake-3.10+-064F8C)](https://cmake.org/)
[![Muduo](https://img.shields.io/badge/Network-Muduo-green)](https://github.com/chenshuo/muduo)
[![MySQL](https://img.shields.io/badge/Database-MySQL+Pool-orange)](https://www.mysql.com/)
[![Redis](https://img.shields.io/badge/Cache-Redis-red)](https://redis.io/)
[![Nginx](https://img.shields.io/badge/LB-Nginx_TCP-green)](https://nginx.org/)

基于 C++11 开发的高并发分布式聊天服务器，完整覆盖从网络层到业务层的全链路实现。支持集群水平扩展、跨服务器消息路由、MySQL 连接池、异步日志，并通过系统化压力测试验证性能。

---

## 目录

- [架构设计](#架构设计)
- [核心功能](#核心功能)
- [项目亮点](#项目亮点)
  - [1. 手写长度头协议解决 TCP 粘包问题](#1-手写长度头协议解决-tcp-粘包问题)
  - [2. MySQL 连接池与性能调优——QPS 提升 52 倍](#2-mysql-连接池与性能调优qps-提升-52-倍)
  - [3. 异步日志系统](#3-异步日志系统)
  - [4. 自研压力测试工具与系统化性能分析](#4-自研压力测试工具与系统化性能分析)
- [压测结果](#压测结果)
- [快速开始](#快速开始)
  - [环境要求](#环境要求)
  - [编译](#编译)
  - [运行](#运行)
  - [集群部署](#集群部署)
- [目录结构](#目录结构)

---

## 架构设计

### 整体架构

```
                          ┌──────────────────┐
                          │   Nginx 负载均衡  │
                          │ (stream 模块 TCP) │
                          └────────┬─────────┘
                                   │
                    ┌──────────────┼──────────────┐
                    ▼              ▼              ▼
            ┌────────────┐ ┌────────────┐ ┌────────────┐
            │ChatServer 1│ │ChatServer 2│ │ChatServer 3│
            │ :6000      │ │ :6001      │ │ :6002      │
            └───┬───┬────┘ └───┬───┬────┘ └───┬───┬────┘
                │   │          │   │          │   │
           ┌────┘   └────┐    │   │   ┌──────┘   └──────┐
           ▼             ▼    ▼   ▼   ▼                 ▼
    ┌──────────┐  ┌──────────┐                         ┌──────────┐
    │  MySQL   │  │  Redis   │      ...                 │  Redis   │
    │ (持久化)  │  │ (跨服通信)│                         │ (跨服通信) │
    └──────────┘  └──────────┘                         └──────────┘
```

### 单节点内部架构

```
┌─────────────────────────────────────────────────┐
│                ChatServer                        │
│                                                  │
│  ┌──────────────────────────────────────────┐   │
│  │      Muduo 主 Reactor (EventLoop)        │   │
│  │    接收连接 → 分发给 Sub Reactor          │   │
│  └──────────────────────────────────────────┘   │
│                       │                          │
│                       ▼                          │
│  ┌──────────────────────────────────────────┐   │
│  │    Sub Reactor 线程池 (默认 4 线程)        │   │
│  │    每个线程持有自己的 EventLoop            │   │
│  │    处理 I/O 事件 + 执行业务回调            │   │
│  └──────────────────────────────────────────┘   │
│                       │                          │
│          ┌────────────┼────────────┐             │
│          ▼            ▼            ▼             │
│   ┌──────────┐ ┌──────────┐ ┌──────────┐        │
│   │ 登录业务  │ │ 消息业务  │ │ 其他业务  │        │
│   │ MySQL认证 │ │ JSON解析  │ │          │        │
│   │          │ │ 路由转发  │ │          │        │
│   └──────────┘ └──────────┘ └──────────┘        │
└─────────────────────────────────────────────────┘
```

### 消息路由流程

```
Client A ──→ Nginx ──→ Server 1
                            │
                  判断 toid 是否在本地
                       │        │
                   是(本地)   否(跨服)
                       │        │
                       ▼        ▼
                  _userConnMap   Redis PUBLISH
                  直接 send     → 其他 Server SUBSCRIBE
                                      │
                                      ▼
                                 _userConnMap
                                  直接 send
                                      │
                                      ▼
                                 Client B 收到消息
```

### 通信协议设计

```
┌───────────────┬──────────────────────────────┐
│  4 字节长度头  │         JSON 消息体           │
│  (uint32_t    │   {"msgid":1,"id":100,...}   │
│   大端序)      │                              │
└───────────────┴──────────────────────────────┘
```

每条消息前附加 4 字节网络字节序（大端）的长度字段，对端先读取 4 字节得到消息体长度 N，再精确读取 N 字节的 JSON 数据。这保证了在 TCP 流式传输中：

- **粘包**：while 循环拆包，每条消息独立解析
- **半包**：字节数不足时 break，等待更多数据到达

---

## 核心功能

| 功能       | 说明                                 | 涉及模块                   |
| :--------- | :----------------------------------- | :------------------------- |
| 用户注册   | 用户名 + 密码注册，写入 MySQL        | UserModel, MySQL Pool      |
| 用户登录   | 密码认证，在线状态管理               | Chatservice, Redis         |
| 一对一聊天 | 消息路由 + 跨服转发                  | Chatservice, Redis Pub/Sub |
| 群组聊天   | 创建 / 加入群组，群发消息            | GroupModel                 |
| 离线消息   | 用户不在线时存储消息，上线后自动推送 | OfflineMessageModel        |
| 好友管理   | 添加好友                             | FriendModel                |
| 心跳保活   | TCP 心跳检测，超时清理僵尸连接       | Muduo TcpConnection        |
| 注销退出   | 下线通知，清理在线状态               | Chatservice, Redis         |

### 消息 ID 映射

| msgid | 方向  | 操作                     |
| :---- | :---- | :----------------------- |
| 1     | C → S | 登录请求                 |
| 2     | S → C | 登录响应（含 errno）     |
| 4     | C → S | 注册请求                 |
| 5     | S → C | 注册响应（含 errno、id） |
| 6     | C → S | 一对一聊天请求           |
| 7     | S → C | 一对一聊天响应（ACK）    |
| 8     | S → C | 服务器主动推送聊天消息   |
| 11    | C → S | 添加好友                 |
| 12    | C → S | 创建群组                 |
| 13    | C → S | 加入群组                 |
| 14    | C → S | 群组聊天                 |

---

## 项目亮点

### 1. 手写长度头协议解决 TCP 粘包问题

#### 背景

TCP 是流式传输协议，数据没有天然的消息边界。当多个 JSON 消息连续发送时，接收端可能一次读取到多条消息（**粘包**），也可能一条消息分多次到达（**半包**）。如果不正确处理，直接调用 `JSON.parse` 会解析失败。

#### 方案

4 字节大端长度头 + JSON 消息体：

```
发送:
  [0x00, 0x00, 0x00, 0x2A] + '{"msgid":1,"id":100,"password":"123456"}'
   └────── 长度头 ──────┘   └─────────── JSON 消息体 ───────────────┘
   length = 42 (0x2A)        实际 42 字节
```

**服务端拆包**（`chatserver.cpp`）：

```cpp
void ChatServer::onMessage(const TcpConnectionPtr &conn, Buffer *buffer, Timestamp time) {
    while (buffer->readableBytes() >= kHeaderLen) {           // 至少 4 字节才能读长度
        int32_t be_len;
        memcpy(&be_len, buffer->peek(), sizeof(be_len));      // 读长度头
        int32_t len = ntohl(be_len);                           // 网络字节序 → 主机字节序
        if (buffer->readableBytes() < kHeaderLen + len) break; // 半包：等更多数据
        buffer->retrieve(kHeaderLen);                          // 消费长度头
        string buf(buffer->peek(), len);                      // 读取 JSON 消息体
        buffer->retrieve(len);                                 // 消费消息体
        json js = json::parse(buf);
        auto handler = ChatService::instance()->getHandler(js["msgid"]);
        handler(conn, js, time);
    }
}
```

**客户端收发**（`main.cpp`）：

```cpp
// 发送：先写 4 字节长度头，再写消息体
static void sendMsg(int fd, const string &msg) {
    uint32_t be_len = htonl(msg.size());
    string frame(reinterpret_cast<const char *>(&be_len), sizeof(be_len));
    frame += msg;
    send(fd, frame.data(), frame.size(), 0);
}

// 接收：先读 4 字节获得长度，再精确读取消息体
static int recvn(int fd, void *buf, size_t n) {
    size_t remaining = n;
    char *p = static_cast<char *>(buf);
    while (remaining > 0) {
        int ret = recv(fd, p, remaining, 0);
        if (ret <= 0) return ret;
        p += ret;
        remaining -= ret;
    }
    return n;
}
```

#### 单元测试验证

9 个测试用例，覆盖所有边界场景：

| 测试用例                 | 验证内容           |
| :----------------------- | :----------------- |
| 单消息收发               | 基本正确性         |
| 粘包（2 条合并）         | while 拆包         |
| 部分消息体（半包）       | 等待更多数据       |
| 分片头部（3+1 字节到达） | 长度头未完整到达   |
| 大消息（5KB）            | 大负载处理         |
| 混合粘包 + 部分消息      | 复杂组合           |
| 空消息                   | 边界处理           |
| 字节序验证               | htonl/ntohl 一致性 |
| 1000 条突发              | 高压稳定           |

#### 结果

**5000 条消息送达率 100%**，无丢包、无解析错误。

---

### 2. MySQL 连接池与性能调优——QPS 提升 52 倍

#### 问题

每次用户登录都执行：

```cpp
MYSQL *conn = mysql_init(nullptr);
mysql_real_connect(conn, ...);   // 耗时 ~150ms
// ... 执行 SQL
mysql_close(conn);               // 销毁连接
```

在高并发下，频繁创建/销毁连接导致：

- TCP 三次握手 + MySQL 认证握手，单次约 50~150ms
- 大量 TIME_WAIT 连接堆积，系统资源浪费
- 登录 QPS 仅 **7**

#### 解决方案：连接池

```
                    ┌─────────────────────────────┐
                    │        连接池（单例）          │
                    │                              │
                    │  ┌─────┐ ┌─────┐ ┌─────┐    │
                    │  │ conn│ │ conn│ │ conn│ ... │
                    │  └─────┘ └─────┘ └─────┘    │
                    │         ▲                     │
                    │         │ 信号量 semaphore     │
                    │   控制并发获取连接上限          │
                    └─────────┼───────────────────┘
                              │
                    ┌─────────┴──────────┐
                    │   shared_ptr +     │
                    │   自定义删除器      │
                    │   (自动归还连接)    │
                    └───────────────────┘
```

核心设计点：

- **生产者-消费者模式**：主线程预先创建 N 个连接，业务线程通过信号量获取
- **智能指针自动归还**：`shared_ptr` 绑定自定义 `deleter`，连接使用完毕后自动归还连接池
- **动态扩容**：`initSize=10`，`maxSize=1024`，高峰自动扩展
- **心跳保活**：定时发送 `SELECT 1`，检测连接有效性，自动移除失效连接

#### 结果

| 配置                     | 登录 QPS | 提升倍数 |
| :----------------------- | :------- | :------- |
| 无连接池（每次新建连接） | ~7       | 1x       |
| 连接池已启用             | **~447** | **~52x** |

---

### 3. 异步日志系统

#### 架构

```
业务线程                        后台线程
┌─────┐                        ┌───────────────┐
│线程1 │──┐   ┌──────────────┐  │               │
├─────┤  │   │              │  │  LogFile       │
│线程2 │──╪═══╡ FrontBuffer │══╪══→(滚动/写入)   │
├─────┤  │   │              │  │               │
│线程3 │──┘   └──────────────┘  │   AppendFile  │
└─────┘                        │   (系统调用)   │
      微秒级写入                  └───────┬───────┘
                                         ▼
                                    磁盘文件
```

- **异步非阻塞**：业务线程仅将日志写入内存缓冲区，磁盘 I/O 由后台线程批量执行
- **双缓冲区交换**：前端缓冲区满时与空的后端缓冲区 `std::swap`，零拷贝，锁持有时间极短
- **文件滚动**：支持按大小（默认 100MB）和按天滚动
- **文件名格式**：`chatserver.20260415-142536.123456.hostname.1234.log`
- **6 级日志**：TRACE / DEBUG / INFO / WARN / ERROR / FATAL，运行时可调

#### 使用示例

```cpp
#include "AsynLogging.hpp"
#include "Logger.hpp"

tulun::AsynLogging g_log("chatserver", 100*1024*1024, 3);

int main() {
    g_log.start();
    tulun::Logger::setOutput([](const string& msg) { g_log.append(msg); });
    tulun::Logger::setFlush([]() { g_log.flush(); });
    tulun::Logger::setLogLevel(tulun::LOG_LEVEL::INFO);

    LOG_INFO << "Server started on port " << 6000;
    LOG_ERROR << "Connection timeout from " << "192.168.1.1";

    g_log.stop();
}
```

---

### 4. 自研压力测试工具与系统化性能分析

手写 C++ 并发压测工具 [`benchmark.cpp`](./test/benchmark/benchmark.cpp)，三阶段测试：

```
阶段1: 顺序注册 N 个用户
  (测量注册 QPS)

阶段2: N 线程并发登录 + 独立计时
  (测量登录吞吐 + P50/P95/P99 延迟)
  ┌──────┐ ┌──────┐ ┌──────┐
  │线程1 │ │线程2 │ │线程3 │ ... 每个线程: send(LOGIN) → recv(ACK) → 计时
  └──────┘ └──────┘ └──────┘

阶段3: 消息收发验证
  - 环形发送: 用户 i → 用户 (i+1) % N
  - 每用户独立接收线程, 全双工
  - 端到端延迟: 消息内嵌 _ts 时间戳
  - 预热 → 正式发送 → 统计送达率
```



---

## 压测结果

测试环境：`127.0.0.1:6000`，50 用户并发，每用户 100 条消息，Muduo 4 线程

### 综合指标

| 指标           | 值       | 可信度                   |
| :------------- | :------- | :----------------------- |
| 注册 QPS       | 152      | ✅ （顺序执行，基准参考） |
| **登录 QPS**   | **447**  | **✅ （并发线程实测）**   |
| 登录 P50 延迟  | 53.9 ms  | ✅                        |
| 登录 P95 延迟  | 97.0 ms  | ✅                        |
| 登录 P99 延迟  | 103.4 ms | ✅                        |
| **消息送达率** | **100%** | **✅ （5000/5000）**      |

### 登录延迟分布

```
最小:   6.6 ms
P50:   53.9 ms   ── 一半用户在此以内完成
P95:   97.0 ms   ── 95% 用户在此以内完成
P99:  103.4 ms   ── 99% 用户在此以内完成
最大:  103.4 ms
```

延迟集中在 50~100ms 区间，主要耗时在 MySQL 认证查询，属于正常范围。连接池启用后，P50 从 ~150ms 降低至 ~54ms。

---

## 快速开始

### 环境要求

- **编译器**：GCC 4.8+（支持 C++11）或更高版本
- **CMake**：3.10+
- **Muduo 网络库**：需预先编译安装
- **MySQL**：5.7+，需创建对应数据库
- **Redis**：3.0+，用于跨服务器通信
- **Nginx**：1.18+（集群部署时需要 `stream` 模块）

### 编译

```bash
git clone <repo-url>
cd chatserver

# 创建构建目录
mkdir -p build && cd build

# 生成构建系统
cmake ..

# 编译全部目标
make -j$(nproc)

# 单独编译某个目标
make ChatServer    # 服务端
make ChatClient    # 客户端
make benchmark     # 压测工具
make test_protocol # 单元测试
```

编译产出在 `bin/` 目录：

```
bin/
├── ChatServer      # 服务端
├── ChatClient      # 客户端
├── benchmark       # 压力测试工具
└── test_protocol   # 协议单元测试
```

### 运行

```bash
# 1. 准备 MySQL
mysql -u root -p -e "CREATE DATABASE IF NOT EXISTS chatserver;"

# 2. 启动 Redis
redis-server &

# 3. 启动服务端
cd bin
./ChatServer 127.0.0.1 6000

# 4. 启动客户端
./ChatClient 127.0.0.1 6000

# 5. 运行单元测试（验证协议正确性）
./test_protocol

# 6. 运行压力测试（可选）
./benchmark 127.0.0.1 6000 50 100
```

> 注：客户端支持交互式输入，输入 `help` 查看命令列表。

### 集群部署

```bash
# 1. 启动多个 ChatServer 实例
./ChatServer 0.0.0.0 6001
./ChatServer 0.0.0.0 6002
./ChatServer 0.0.0.0 6003

# 2. 配置 Nginx（nginx.conf）
stream {
    upstream chatserver {
        server 127.0.0.1:6001;
        server 127.0.0.1:6002;
        server 127.0.0.1:6003;
    }
    server {
        listen 8000;
        proxy_pass chatserver;
    }
}

# 3. 启动 Nginx
nginx -c /path/to/nginx.conf

# 4. 客户端统一连接 Nginx
./ChatClient 127.0.0.1 8000
```

---

## 目录结构

```
chatserver/
├── include/                        # 头文件
│   ├── asynlog/                    #   异步日志系统
│   │   ├── AsynLogging.hpp         #     异步日志核心（生产者-消费者）
│   │   ├── Logger.hpp              #     日志外观（LOG_INFO / LOG_ERROR 等）
│   │   ├── LogFile.hpp             #     文件滚动管理
│   │   ├── LogMessage.hpp          #     日志消息封装
│   │   ├── AppendFile.hpp          #     文件 I/O 封装
│   │   ├── Timestamp.hpp           #     高精度时间戳
│   │   ├── CountDownLatch.hpp      #     线程同步门闩
│   │   └── LogCommon.hpp          #     日志级别等常量
│   ├── server/                     #   业务模块
│   │   ├── chatserver.hpp          #     Muduo 网络层封装
│   │   ├── chatservice.hpp         #     业务分发层
│   │   ├── db/                     #     MySQL 连接池头文件
│   │   ├── model/                  #     数据模型（User/Group/Friend/Offline）
│   │   └── redis/                  #     Redis 封装
│   └── public.hpp                  #   公共常量
├── src/                            # 源文件
│   ├── server/                     #   服务端
│   │   ├── chatserver.cpp          #     Muduo 回调（含长度头拆包）
│   │   ├── chatservice.cpp         #     业务处理器
│   │   ├── main.cpp                #     服务端入口
│   │   ├── db/                     #     连接池实现
│   │   ├── model/                  #     数据模型实现
│   │   └── redis/                  #     Redis 实现
│   ├── client/main.cpp             #   客户端（含长度头收发 + 交互界面）
│   └── asynlog/                    #   日志系统实现
├── test/                           # 测试工具
│   ├── test_protocol/              #   协议单元测试（9 用例）
│   │   ├── test_protocol.cpp
│   │   └── CMakeLists.txt
│   ├── benchmark/                  #   压力测试工具
│   │   ├── benchmark.cpp
│   │   ├── 压测说明.md             #     测试架构 & 使用指南
│   │   └── 压测结果.md             #     测试数据 & 分析
│   ├── testJson/                   #   JSON 测试
│   └── testmuduo/                  #   Muduo 框架测试
├── thirdparty/
│   └── json.hpp                    # nlohmann/json 单头文件库
├── 项目文档/
│   ├── TCP粘包解决方案.md           # 协议设计文档
│   └── 架构分析文档.md
├── CMakeLists.txt                  # 顶层 CMake 构建配置
├── autobuild.sh                    # 自动编译脚本
└── mysql.cnf                       # MySQL 连接配置
```

