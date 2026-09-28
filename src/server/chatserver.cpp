#include "chatserver.hpp"
#include "chatservice.hpp"
#include "public.hpp"
#include "json.hpp"
#include "Logger.hpp"
#include <functional>
#include <string>
#include <cstring>   // memcpy
#include <arpa/inet.h>   // ntohl
using namespace std;
using namespace placeholders;
using json = nlohmann::json;

// 初始化聊天服务器对象
ChatServer::ChatServer(EventLoop *loop,
                       const InetAddress &listenAddr,
                       const string &nameArg)
    : _server(loop, listenAddr, nameArg), _loop(loop)
{
    _server.setConnectionCallback(std::bind(&ChatServer::onConnection, this, _1));
    _server.setMessageCallback(std::bind(&ChatServer::onMessage, this, _1, _2, _3));
    _server.setThreadNum(4);
}

// 启动服务
void ChatServer::start()
{
    _server.start();
    LOG_INFO << "ChatServer started, listening on "
             << _server.ipPort();
}

// 上报连接相关信息的回调函数
void ChatServer::onConnection(const TcpConnectionPtr &conn)
{
    if (conn->connected())
    {
        LOG_INFO << "New connection: " << conn->peerAddress().toIpPort();
    }
    // 客户端断开连接
    else
    {
        LOG_INFO << "Connection closed: " << conn->peerAddress().toIpPort();
        ChatService::instance()->clientCloseException(conn);
        conn->shutdown();
    }
}

// 上报读写事件相关信息的回调函数（长度头协议，解决TCP粘包）
void ChatServer::onMessage(const TcpConnectionPtr &conn,
                           Buffer *buffer,
                           Timestamp time)
{
    while (buffer->readableBytes() >= static_cast<size_t>(kHeaderLen))
    {
        // 1. 读4字节长度头（网络字节序 → 主机字节序）
        int32_t be_len;
        ::memcpy(&be_len, buffer->peek(), sizeof(be_len));
        int32_t len = ntohl(be_len);

        // 2. 检查是否收全了整条消息
        if (buffer->readableBytes() < static_cast<size_t>(kHeaderLen + len))
        {
            break;  // 数据未收全，等下次回调
        }

        // 3. 消耗长度头 + 消息体
        buffer->retrieve(kHeaderLen);
        string buf(buffer->peek(), len);
        buffer->retrieve(len);

        // 4. 反序列化 + 业务分发
        json js = json::parse(buf);
        int msgid = js["msgid"].get<int>();
        LOG_DEBUG << "收到来自 " << conn->peerAddress().toIpPort()
                  << " 的消息, msgid=" << msgid << ", 消息体长度=" << len;
        auto msgHandler = ChatService::instance()->getHandler(msgid);

        // 投递到线程池异步处理，I/O 线程立即返回继续接收
        ChatService::instance()->getThreadPool().AddTask(
            [conn, js = std::move(js), time, msgHandler]() mutable {
                msgHandler(conn, js, time);
            });
    }
}