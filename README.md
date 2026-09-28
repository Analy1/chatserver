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
  - [5. 应用层心跳检测机制](#5-应用层心跳检测机制)
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
| 心跳保活   | 应用层心跳检测，5s 间隔/10s 超时     | 客户端线程 + 服务端定时器   |
|             | 超时自动关闭 + 清理在线/Redis/DB   | Chatservice, Muduo          |
| 注销退出   | 下线通知，清理在线状态               | Chatservice, Redis         |

### 消息 ID 映射

| msgid | 方向  | 操作                                     |
| :---- | :---- | :--------------------------------------- |
| 1     | C → S | 登录请求                                 |
| 2     | S → C | 登录响应（含 errno）                     |
| 3     | C → S | 注销请求                                 |
| 4     | C → S | 注册请求                                 |
| 5     | S → C | 注册响应（含 errno、id）                 |
| 6     | C ⇄ S | 一对一聊天（C→S 请求，S→C 转发给接收方） |
| 7     | C → S | 添加好友                                 |
| 8     | C → S | 创建群组                                 |
| 9     | C → S | 加入群组                                 |
| 10    | C ⇄ S | 群组聊天（C→S 请求，S→C 群发给成员）     |
| 99    | C → S | 心跳保活                                 |

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

#### 验证方式

协议在真实 TCP 链路上的正确性由压力测试端到端验证：50 用户并发、每用户 100 条消息共 5000 条，**送达率 100%**，无丢包、无解析错误（详见 [压测结果](#压测结果)）。

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

### 5. 应用层心跳检测机制

#### 背景

TCP 连接断开时，服务端并**不总是能立即感知**：

| 场景 | 服务端感知方式 | 延迟 |
|:---|:---|:---|
| 客户端正常退出 | 收到 FIN，触发 `onConnection(disconnected)` | 即时 |
| 客户端进程崩溃 | 操作系统关闭 socket，发送 FIN/RST | 即时 |
| 客户端断网 / 机器宕机 | **没有任何通知** | **永远不知道** |
| 网络中间设备断开 | 同上 | 同上 |

操作系统自带的 `TCP Keepalive` 默认 2 小时才发送一次探测，不适用于即时通信场景。因此必须由应用层自行实现心跳检测，**快速发现并清理死连接**。

#### 整体设计

```
客户端                              服务端
  │                                  │
  │  ─── 登录成功 ─────────────────  │
  │                                  │
  │  [启动心跳线程]                  │  [启动超时扫描器]
  │   每 5s 发送心跳                  │   每 5s 扫描所有连接
  │                                  │
  │  ─── HEART_BEAT_MSG(id=1) ───   │
  │                                  │  _connLastHeartBeat[conn] = now()
  │  ─── HEART_BEAT_MSG(id=1) ───   │
  │  ...                             │  ...
  │                                  │
  │  [客户端断网]                    │
  │  (无法发送)                      │
  │                                  │  [00:00  扫描] 正常
  │                                  │  [00:05  扫描] 正常
  │                                  │  [00:10  扫描] 超过 10s → 超时!
  │                                  │    ├─ clientCloseException()
  │                                  │    │   ├─ _userConnMap.erase
  │                                  │    │   ├─ redis.unsubscribe
  │                                  │    │   ├─ user.setState(offline)
  │                                  │    │   └─ _connLastHeartBeat.erase
  │                                  │    └─ conn->shutdown()
  │                                  │
  │  [网络恢复]                       │
  │  send(心跳) → 连接已关闭          │
  │  recv → 0 → 进程退出              │
```

#### 客户端实现

**启动时机**：用户登录成功后、进入主菜单前。

**线程模型**：一个后台 detached 线程循环发送，通过 `g_heartBeatRunning` 原子标志控制生命周期。

```cpp
// 客户端心跳线程
atomic<bool> g_heartBeatRunning{false};

void startHeartBeatTask(int clientfd, int userid) {
    g_heartBeatRunning = true;
    thread([=]() {
        while (g_heartBeatRunning) {
            json js;
            js["msgid"] = HEART_BEAT_MSG;
            js["id"] = userid;              // 告诉服务端谁还活着
            if (sendMsg(clientfd, js.dump()) == -1) break;  // 连接断开，自动退出
            sleep(5);                       // 5 秒间隔
        }
    }).detach();
}
```

**退出机制**：始终遵循"先停心跳，再关连接"的原则。

```
loginout（主动注销）:
  g_heartBeatRunning = false;        // ① 停心跳
  send(LOGOUT_MSG);                  // ② 发注销请求
  isMainMenuRunning = false;         // ③ 退出主菜单

quit（退出程序）:
  g_heartBeatRunning = false;        // ① 停心跳
  close(clientfd);                   // ② 关连接
  exit(0);                           // ③ 退出进程
```

这个顺序保证心跳线程不会在 socket 已关闭后还尝试发送数据。

#### 服务端实现

服务端心跳处理由两个独立任务构成：

**任务一：心跳消息处理器（`heartBeat`）**

每次收到客户端心跳时触发，只做一件事——更新该连接的最后心跳时间。

```cpp
void ChatService::heartBeat(const TcpConnectionPtr &conn, json &js, Timestamp) {
    int userid = js["id"].get<int>();
    {
        lock_guard<mutex> lock(_heartBeatMutex);
        _connLastHeartBeat[conn] = Timestamp::now();  // 记录当前时间
    }
    // 单向心跳，不给客户端回复
}
```

关键点：
- **key 是连接对象** `TcpConnectionPtr`，不是用户 ID。因为连接断开重建后是新的对象
- **单向心跳**，服务端不回复。双向确认对即时通信没有额外收益，只会浪费带宽

**任务二：超时扫描定时器（`checkHeartBeatTimeout`）**

由 EventLoop 的 `runEvery` 驱动，每 5 秒执行一次，不占用独立线程。

```cpp
// 在 main.cpp 中注册
loop.runEvery(5.0, []() { ChatService::instance()->checkHeartBeatTimeout(); });
```

扫描逻辑：

```cpp
void ChatService::checkHeartBeatTimeout() {
    Timestamp now = Timestamp::now();
    vector<TcpConnectionPtr> needClose;

    {
        lock_guard<mutex> lock(_heartBeatMutex);
        for (auto &pair : _connLastHeartBeat) {
            // 当前时间 - 最后心跳时间 > 10 秒 → 超时
            if (timeDifference(now, pair.second) > 10.0) {
                needClose.push_back(pair.first);
            }
        }
        for (auto &conn : needClose) {
            _connLastHeartBeat.erase(conn);  // 先删除，防内存泄漏
        }
    }

    // 出锁后再关闭连接（避免死锁：clientCloseException 内部需要 _connMutex）
    for (auto &conn : needClose) {
        clientCloseException(conn);  // 清理业务数据
        conn->shutdown();            // 关闭 TCP 连接
    }
}
```

**超时关闭的完整清理链**：

```
checkHeartBeatTimeout
  └─ clientCloseException(conn)
       ├─ _userConnMap.erase                      # ① 删除在线映射
       ├─ redis.unsubscribe(user.getId())          # ② 取消 Redis 订阅
       ├─ user.setState("offline")                 # ③ 数据库状态离线
       │   └─ _userModel.updateState(user)
       └─ _connLastHeartBeat.erase(conn)           # ④ 删除心跳记录
  └─ conn->shutdown()                              # ⑤ 关闭 TCP 连接
       └─ muduo 触发 onConnection(disconnected)    # 确认关闭
```

#### 关键参数

| 参数 | 值 | 说明 |
|:---|:---|:---|
| 客户端心跳间隔 | 5 秒 | 线程 `sleep(5)` |
| 服务端扫描间隔 | 5 秒 | `loop.runEvery(5.0, ...)` |
| 超时阈值 | 10 秒 | 心跳间隔的 2 倍，容忍一次丢包 |
| 键类型 | `TcpConnectionPtr` | 连接对象指针，非用户 ID |
| 锁定策略 | 锁内收集 + 锁外关闭 | 避免多锁死锁 |

#### 边界情况处理

| 场景 | 处理方式 |
|:---|:---|
| 网络短暂闪断（<10s） | 超时阈值内恢复，连接不受影响 |
| 服务端宕机 | 客户端 `recv` 返回 0，接收线程退出进程 |
| 客户端进程崩溃 | 操作系统关闭 socket，服务端收到 FIN → `onConnection` |
| 同一用户重复登录 | 两次独立的 `TcpConnection`，心跳记录互不干扰，旧连接超时后自动清理 |

#### 为什么不用 TCP Keepalive？

| 特性 | TCP Keepalive | 应用层心跳 |
|:---|:---|:---|
| 默认探测间隔 | 2 小时 | 5 秒 |
| 携带业务信息 | 否（无用户 ID） | 是（可携带 userid） |
| 可配置性 | 需修改系统参数 | 代码层随意调整 |
| 适用场景 | 长连接保活 | **即时通信心跳检测** |

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
```

编译产出在 `bin/` 目录：

```
bin/
├── ChatServer      # 服务端
├── ChatClient      # 客户端
└── benchmark       # 压力测试工具
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

# 5. 运行压力测试（可选，需服务端已启动）
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
│   │   └── LogCommon.hpp           #     日志级别等常量
│   ├── server/                     #   业务模块
│   │   ├── chatserver.hpp          #     Muduo 网络层封装
│   │   ├── chatservice.hpp         #     业务分发层
│   │   ├── db/                     #     MySQL 连接池头文件
│   │   ├── model/                  #     数据模型（User/Group/Friend/Offline）
│   │   ├── redis/                  #     Redis 封装
│   │   └── threadpool/             #     工作窃取线程池
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
│   ├── benchmark/                  #   压力测试工具
│   │   └── benchmark.cpp
│   ├── testJson/                   #   JSON 测试
│   └── testmuduo/                  #   Muduo 框架测试
├── thirdparty/
│   └── json.hpp                    # nlohmann/json 单头文件库
├── CMakeLists.txt                  # 顶层 CMake 构建配置
├── autobuild.sh                    # 自动编译脚本
└── mysql.cnf                       # MySQL 连接配置
```

