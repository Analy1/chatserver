// ================================================================
// ChatServer 压测工具
// 测试指标: 登录 QPS、消息吞吐、延迟分布 (P50/P95/P99)
// 协议: 4字节长度头 + JSON 消息体
// 用法: ./benchmark [ip] [port] [num_users] [msgs_per_user]
// ================================================================

#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <chrono>
#include <cmath>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <ctime>
#include <mutex>

#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

using namespace std;
using namespace chrono;

// ═════════════════════════════════════════════════════════════
// 协议层
// ═════════════════════════════════════════════════════════════

static int sendMsg(int fd, const string &msg)
{
    uint32_t be_len = htonl(static_cast<uint32_t>(msg.size()));
    string buf;
    buf.append(reinterpret_cast<const char *>(&be_len), sizeof(be_len));
    buf.append(msg);
    return send(fd, buf.data(), buf.size(), 0);
}

static int recvn(int fd, void *buf, size_t n)
{
    size_t remaining = n;
    char *p = static_cast<char *>(buf);
    while (remaining > 0)
    {
        int ret = recv(fd, p, remaining, 0);
        if (ret <= 0) return ret;
        p += ret;
        remaining -= ret;
    }
    return static_cast<int>(n);
}

// ═════════════════════════════════════════════════════════════
// 配置
// ═════════════════════════════════════════════════════════════

struct Config
{
    string ip = "127.0.0.1";
    uint16_t port = 6000;
    int num_users = 50;
    int msgs_per_user = 100;
    int warmup_sec = 2;
};

static Config g_cfg;

// ═════════════════════════════════════════════════════════════
// 延迟统计
// ═════════════════════════════════════════════════════════════

struct LatencyStats
{
    vector<double> samples;
    mutex mtx;

    void add(double ms)
    {
        lock_guard<mutex> lock(mtx);
        samples.push_back(ms);
    }

    void report(const string &phase)
    {
        if (samples.empty()) { cout << "  " << phase << ": 无样本" << endl; return; }
        sort(samples.begin(), samples.end());
        size_t n = samples.size();
        double sum = 0;
        for (auto v : samples) sum += v;
        cout << "  " << phase << ":" << endl;
        cout << "    样本数: " << n << endl;
        cout << "    平均:   " << (sum / n) << " ms" << endl;
        cout << "    最小:   " << samples.front() << " ms" << endl;
        cout << "    最大:   " << samples.back() << " ms" << endl;
        cout << "    P50:    " << samples[n / 2] << " ms" << endl;
        cout << "    P95:    " << samples[static_cast<size_t>(n * 0.95)] << " ms" << endl;
        cout << "    P99:    " << samples[static_cast<size_t>(n * 0.99)] << " ms" << endl;
    }
};

// ═════════════════════════════════════════════════════════════
// TCP 连接
// ═════════════════════════════════════════════════════════════

static int tcp_connect(const string &ip, uint16_t port)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = inet_addr(ip.c_str());
    if (connect(fd, (sockaddr *)&addr, sizeof(addr)) < 0)
    {
        close(fd);
        return -1;
    }
    return fd;
}

// ═════════════════════════════════════════════════════════════
// 虚拟用户
// ═════════════════════════════════════════════════════════════

struct VirtualUser
{
    int fd = -1;
    int userid = 0;
    string name;
    double login_latency_ms = 0;
    vector<double> send_latencies;
    LatencyStats e2e_latency;         // 端到端延迟（发送者 → 接收者）
    atomic<int> recv_count{0};
    thread recv_thread;

    ~VirtualUser()
    {
        if (recv_thread.joinable())
        {
            if (fd >= 0) close(fd);
            fd = -1;
            recv_thread.join();
        }
        else if (fd >= 0)
        {
            close(fd);
        }
    }

    // 注册
    bool reg()
    {
        name = "bench_" + to_string(rand() % 100000 + 1);
        string msg = R"({"msgid":4,"name":")" + name + R"(","password":"123456"})";
        if (sendMsg(fd, msg) < 0) return false;

        int32_t be_len;
        if (recvn(fd, &be_len, sizeof(be_len)) <= 0) return false;
        int32_t len = ntohl(be_len);
        string buf(len, 0);
        if (recvn(fd, &buf[0], len) <= 0) return false;

        auto pos = buf.find("\"id\":");
        if (pos == string::npos) return false;
        userid = atoi(buf.c_str() + pos + 5);
        return userid > 0;
    }

    // 登录
    bool login()
    {
        string msg = R"({"msgid":1,"id":)" + to_string(userid) + R"(,"password":"123456"})";
        auto t1 = high_resolution_clock::now();
        if (sendMsg(fd, msg) < 0) return false;

        int32_t be_len;
        if (recvn(fd, &be_len, sizeof(be_len)) <= 0) return false;
        int32_t len = ntohl(be_len);
        string buf(len, 0);
        if (recvn(fd, &buf[0], len) <= 0) return false;
        auto t2 = high_resolution_clock::now();

        login_latency_ms = duration<double, milli>(t2 - t1).count();
        return (buf.find("\"errno\":0") != string::npos);
    }

    // 发送单聊消息（嵌入时间戳用于端到端延迟测量）
    bool sendOneChat(int toid, const string &content, bool warmup)
    {
        auto now = duration_cast<microseconds>(
            high_resolution_clock::now().time_since_epoch()).count();
        string msg = R"({"msgid":6,"id":)" + to_string(userid) +
                     R"(,"toid":)" + to_string(toid) +
                     R"(,"name":")" + name +
                     R"(","msg":")" + content +
                     R"(","time":"00:00:00","_ts":)" + to_string(now) + "}";
        auto t1 = high_resolution_clock::now();
        if (sendMsg(fd, msg) < 0) return false;
        auto t2 = high_resolution_clock::now();
        if (!warmup)
            send_latencies.push_back(duration<double, milli>(t2 - t1).count());
        return true;
    }

    // 启动接收线程（解析 _ts 时间戳，测量端到端延迟）
    void startRecvLoop()
    {
        recv_thread = thread([this]() {
            while (fd >= 0)
            {
                int32_t be_len;
                int ret = recvn(fd, &be_len, sizeof(be_len));
                if (ret <= 0) return;
                int32_t len = ntohl(be_len);
                string buf(len, 0);
                if (recvn(fd, &buf[0], len) <= 0) return;

                recv_count++;

                // 从 JSON 中提取 _ts 字段（简单字符串搜索，无需 JSON 库）
                auto ts_pos = buf.find("\"_ts\":");
                if (ts_pos == string::npos) continue;

                // 跳过 "_ts":（6 个字符），到达数字
                ts_pos += 6;
                long long send_ts = atoll(buf.c_str() + ts_pos);

                auto now = duration_cast<microseconds>(
                    high_resolution_clock::now().time_since_epoch()).count();
                double e2e_ms = (now - send_ts) / 1000.0;
                e2e_latency.add(e2e_ms);
            }
        });
    }
};

// ═════════════════════════════════════════════════════════════
// 阶段1: 注册
// ═════════════════════════════════════════════════════════════

static vector<VirtualUser *> phase_registration(vector<VirtualUser> &users)
{
    cout << "\n════════ 阶段1: 注册 ════════" << endl;
    cout << "  目标: " << g_cfg.num_users << " 用户" << endl;

    auto t1 = high_resolution_clock::now();
    int succ = 0;
    vector<VirtualUser *> online;

    for (int i = 0; i < g_cfg.num_users; i++)
    {
        auto &u = users[i];
        u.fd = tcp_connect(g_cfg.ip, g_cfg.port);
        if (u.fd < 0) { cerr << "  [失败] " << i << " 连接" << endl; continue; }
        if (!u.reg()) { cerr << "  [失败] " << i << " 注册" << endl; close(u.fd); u.fd = -1; continue; }
        succ++;
        online.push_back(&u);
        if (succ % 10 == 0) cout << "  已注册 " << succ << "/" << g_cfg.num_users << endl;
    }
    auto t2 = high_resolution_clock::now();
    double sec = duration<double>(t2 - t1).count();
    cout << "  完成: " << succ << "/" << g_cfg.num_users << " 成功" << endl;
    cout << "  耗时: " << sec << " 秒" << endl;
    cout << "  注册 QPS: " << (sec > 0 ? succ / sec : 0) << endl;
    return online;
}

// ═════════════════════════════════════════════════════════════
// 阶段2: 并发登录
// ═════════════════════════════════════════════════════════════

static double phase_login(vector<VirtualUser *> &users)
{
    cout << "\n════════ 阶段2: 并发登录 ════════" << endl;
    cout << "  目标: " << users.size() << " 用户" << endl;

    atomic<int> succ{0}, fail{0};
    LatencyStats lat;
    auto t1 = high_resolution_clock::now();
    {
        vector<thread> threads;
        for (auto *u : users)
            threads.emplace_back([u, &succ, &fail, &lat]() {
                if (u->login()) { succ++; lat.add(u->login_latency_ms); }
                else fail++;
            });
        for (auto &t : threads) t.join();
    }
    auto t2 = high_resolution_clock::now();
    double sec = duration<double>(t2 - t1).count();
    cout << "  成功: " << succ.load() << "/" << (succ.load() + fail.load()) << endl;
    cout << "  耗时: " << sec << " 秒" << endl;
    cout << "  登录 QPS: " << (sec > 0 ? succ.load() / sec : 0) << endl;
    lat.report("登录延迟");
    return (sec > 0 ? succ.load() / sec : 0);
}

// ═════════════════════════════════════════════════════════════
// 阶段3: 消息发送
// ═════════════════════════════════════════════════════════════

static double phase_messaging(vector<VirtualUser *> &users)
{
    int n = static_cast<int>(users.size());
    if (n < 2) { cout << "\n  消息测试: 需要 >=2 用户，跳过" << endl; return 0; }

    cout << "\n════════ 阶段3: 消息 ════════" << endl;
    cout << "  用户 i -> 用户 (i+1)%n" << endl;
    cout << "  每用户 " << g_cfg.msgs_per_user << " 条" << endl;

    for (auto *u : users) u->startRecvLoop();
    this_thread::sleep_for(chrono::milliseconds(500));

    cout << "  预热 " << g_cfg.warmup_sec << " 秒..." << endl;
    for (int i = 0; i < min(5, g_cfg.msgs_per_user); i++)
    {
        for (int j = 0; j < n; j++)
            users[j]->sendOneChat(users[(j + 1) % n]->userid, "warmup", true);
        this_thread::sleep_for(chrono::milliseconds(10));
    }
    this_thread::sleep_for(chrono::seconds(g_cfg.warmup_sec));

    // 重置接收计数，排除预热消息
    for (auto *u : users) u->recv_count.store(0);

    cout << "  发送中..." << endl;
    atomic<long> total_sends{0}, send_fails{0};
    auto t1 = high_resolution_clock::now();
    {
        vector<thread> senders;
        for (int i = 0; i < n; i++)
            senders.emplace_back([i, n, &users, &total_sends, &send_fails]() {
                int target = (i + 1) % n;
                for (int k = 0; k < g_cfg.msgs_per_user; k++)
                {
                    string c = "m" + to_string(i) + "_" + to_string(k);
                    if (users[i]->sendOneChat(users[target]->userid, c, false))
                        total_sends++;
                    else
                        send_fails++;
                }
            });
        for (auto &t : senders) t.join();
    }
    auto t2 = high_resolution_clock::now();
    double sec = duration<double>(t2 - t1).count();

    LatencyStats slat, elat;
    for (auto *u : users)
    {
        for (auto v : u->send_latencies)
            slat.add(v);
        // 合并 e2e 延迟
        lock_guard<mutex> lock(u->e2e_latency.mtx);
        for (auto v : u->e2e_latency.samples)
            elat.add(v);
    }

    this_thread::sleep_for(chrono::seconds(5));
    long total_recv = 0;
    for (auto *u : users) total_recv += u->recv_count.load();

    double qps = total_sends.load() / sec;
    cout << "  发送: " << total_sends.load() << " / 失败: " << send_fails.load() << endl;
    cout << "  耗时: " << sec << " 秒" << endl;
    cout << "  吞吐: " << qps << " msg/s" << endl;
    slat.report("发送延迟（本地 send()）");
    elat.report("端到端延迟（发送→接收）");
    cout << "  接收: " << total_recv << " / 期望: " << n * g_cfg.msgs_per_user << endl;
    cout << "  送达率: " << (n * g_cfg.msgs_per_user > 0
        ? 100.0 * total_recv / (n * g_cfg.msgs_per_user) : 0) << "%" << endl;

    for (auto *u : users) if (u->recv_thread.joinable()) u->recv_thread.detach();
    return qps;
}

// ═════════════════════════════════════════════════════════════
// 主函数
// ═════════════════════════════════════════════════════════════

int main(int argc, char *argv[])
{
    if (argc >= 2) g_cfg.ip = argv[1];
    if (argc >= 3) g_cfg.port = static_cast<uint16_t>(atoi(argv[2]));
    if (argc >= 4) g_cfg.num_users = atoi(argv[3]);
    if (argc >= 5) g_cfg.msgs_per_user = atoi(argv[4]);

    srand(static_cast<unsigned>(time(nullptr)));

    cout << "╔══════════════════════════════════════╗" << endl;
    cout << "║    ChatServer 压力测试工具           ║" << endl;
    cout << "╚══════════════════════════════════════╝" << endl;
    cout << "服务器: " << g_cfg.ip << ":" << g_cfg.port << endl;
    cout << "用户数: " << g_cfg.num_users << endl;
    cout << "每用户消息: " << g_cfg.msgs_per_user << endl;

    vector<VirtualUser> users(g_cfg.num_users);
    auto online = phase_registration(users);
    if (online.empty()) { cerr << "无注册用户，退出" << endl; return 1; }

    double login_qps = phase_login(online);
    double msg_qps = phase_messaging(online);

    cout << "\n════════ 汇总 ════════" << endl;
    cout << "  配置: " << g_cfg.num_users << " 用户 x " << g_cfg.msgs_per_user << " 消息" << endl;
    cout << "  登录 QPS: " << login_qps << endl;
    cout << "  消息吞吐: " << msg_qps << " msg/s" << endl;
    cout << "════════════════════════" << endl;
    return 0;
}
