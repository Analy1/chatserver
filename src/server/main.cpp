#include "AsynLogging.hpp"
#include "Logger.hpp"
#include "chatserver.hpp"
#include "chatservice.hpp"
#include <iostream>
#include <signal.h>
#include "server/db/CommonConnectionPool.h"



using namespace std;

//  全局异步日志对象（日志文件以 "chatserver" 为基础名）
tulun::AsynLogging g_asyncLog("chatserver",
                              100 * 1024 * 1024, // 100MB 滚动
                              3);                // 3秒刷新

//  输出回调：把日志交给异步日志对象
void asyncOutput(const std::string &msg)
{
    g_asyncLog.append(msg);
}

//  刷新回调
void asyncFlush()
{
    g_asyncLog.flush();
}

// 处理服务器ctrl+c结束后，重置user的状态信息
#include <atomic>

void resetHandler(int sig)
{
    static std::atomic<int> count{0};
    count++;
    
    if (count == 1)
    {
        // 第一次 Ctrl+C：优雅退出
        LOG_INFO << "Received signal " << sig << ", shutting down gracefully...";
        g_asyncLog.stop();
        ChatService::instance()->reset();
        LOG_INFO << "Server stopped";
        exit(0);
    }
    else
    {
        // 第二次 Ctrl+C：强制退出
        LOG_WARN << "Force exit!";
        _exit(0);  // _exit 直接退出，不做任何清理
    }
}

int main(int argc, char **argv)
{

    // 1. 启动异步日志（最先启动）
    g_asyncLog.start();
    tulun::Logger::setOutput(asyncOutput);
    tulun::Logger::setFlush(asyncFlush);
    tulun::Logger::setLogLevel(tulun::LOG_LEVEL::DEBUG); 

    ConnectionPool::getConnectionPool();

    if (argc < 3)
    {
        cerr << "command invalid! example: ./ChatClient 127.0.0.1 6000" << endl;
        exit(-1);
    }

    // 解析通过命令行参数传递的ip和port
    char *ip = argv[1];
    uint16_t port = atoi(argv[2]);

    LOG_DEBUG << "Server listening on " << ip << ":" << port;

    signal(SIGINT, resetHandler);

    EventLoop loop;
    InetAddress addr(ip, port);
    ChatServer server(&loop, addr, "ChatServer");

    // loop.runEvery(5.0, std::bind(&ChatService::checkHeartBeatTimeout, ChatService::instance()));
    loop.runEvery(5.0, []()
                  { ChatService::instance()->checkHeartBeatTimeout(); });

    server.start();
    LOG_DEBUG << "ChatServer started successfully";
    loop.loop();
    return 0;
}