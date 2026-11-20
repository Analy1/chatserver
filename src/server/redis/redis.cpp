#include "redis.hpp"
#include "Logger.hpp"
#include <iostream>
#include <poll.h>
#include <mutex>

using namespace std;

Redis::Redis()
    : _publish_context(nullptr), _subcribe_context(nullptr)
{
}

Redis::~Redis()
{
    _running = false; // 1. 通知子线程退出

    if (_observer_thread.joinable()) {
        _observer_thread.join(); // 3. 等待子线程彻底执行完毕
    }

    if (_publish_context != nullptr)
    {
        redisFree(_publish_context);
    }

    if (_subcribe_context != nullptr)
    {
        redisFree(_subcribe_context);
    }
}

bool Redis::connect()
{
    // 负责publish发布消息的上下文连接
    _publish_context = redisConnect("127.0.0.1", 6379);
    if (nullptr == _publish_context)
    {
        // cerr << "connect redis failed!" << endl;
        LOG_ERROR << "Redis publish context connect failed";
        return false;
    }

    // 负责subscribe订阅消息的上下文连接
    _subcribe_context = redisConnect("127.0.0.1", 6379);
    if (nullptr == _subcribe_context)
    {
        // cerr << "connect redis failed!" << endl;
        LOG_ERROR << "Redis subscribe context connect failed";
        return false;
    }

    // 在单独的线程中，监听通道上的事件，有消息给业务层进行上报
    _observer_thread = std::thread([this]() {
        observer_channel_message();
    });

    // cout << "connect redis-server success!" << endl;
    LOG_INFO << "Redis connected successfully";
    return true;
}

// 向redis指定的通道channel发布消息
bool Redis::publish(int channel, string message)
{
    /*
    等价于Redis命令：PUBLISH 1 "msg"
    redisCommand(连接，"命令 %d %s",参数)
    */
    redisReply *reply = (redisReply *)redisCommand(_publish_context, "PUBLISH %d %s", channel, message.c_str());
    if (nullptr == reply)
    {
        // cerr << "publish command failed!" << endl;
        LOG_ERROR << "Redis publish command failed, channel=" << channel;
        return false;
    }
    freeReplyObject(reply);
    LOG_DEBUG << "Redis发布消息到通道 channel=" << channel << " 成功";
    return true;
}

// 向redis指定的通道subscribe订阅消息(异步/非阻塞)
bool Redis::subscribe(int channel)
{
    // SUBSCRIBE命令本身会造成线程阻塞等待通道里面发生消息，这里只做订阅通道，不接收通道消息
    // 通道消息的接收专门在observer_channel_message函数中的独立线程中进行
    // 只负责发送命令，不阻塞接收redis server响应消息，否则和notifyMsg线程抢占响应资源（只发送，不等待回复）
    if (REDIS_ERR == redisAppendCommand(this->_subcribe_context, "SUBSCRIBE %d", channel))
    {
        // cerr << "subscribe command failed!" << endl;
        LOG_ERROR << "Redis subscribe command failed, channel=" << channel;
        return false;
    }
    // redisBufferWrite可以循环发送缓冲区，直到缓冲区数据发送完毕（done被置为1）
    int done = 0;
    while (!done)
    {
        if (REDIS_ERR == redisBufferWrite(this->_subcribe_context, &done))
        {
            // cerr << "subscribe command failed!" << endl;
            LOG_ERROR << "Redis buffer write failed on subscribe";
            return false;
        }
    }
    // redisGetReply
    LOG_INFO << "Redis订阅通道 channel=" << channel << " 成功";

    return true;
}

// 向redis指定的通道unsubscribe取消订阅消息
bool Redis::unsubscribe(int channel)
{
    if (REDIS_ERR == redisAppendCommand(this->_subcribe_context, "UNSUBSCRIBE %d", channel))
    {
        // cerr << "unsubscribe command failed!" << endl;
        LOG_ERROR << "Redis unsubscribe command failed, channel=" << channel;
        return false;
    }
    // redisBufferWrite可以循环发送缓冲区，直到缓冲区数据发送完毕（done被置为1）
    int done = 0;
    while (!done)
    {
        if (REDIS_ERR == redisBufferWrite(this->_subcribe_context, &done))
        {
            // cerr << "unsubscribe command failed!" << endl;
            LOG_ERROR << "Redis buffer write failed on unsubscribe";
            return false;
        }
    }
    LOG_INFO << "Redis取消订阅通道 channel=" << channel << " 成功";
    return true;
}

// 在独立线程中接收订阅通道中的消息
// 订阅连接同时被业务线程用于 SUBSCRIBE/UNSUBSCRIBE，hiredis 连接不是线程安全的，
// 所以读和写都用 _cmdMutex 串行化；用 poll 先等数据，避免持锁阻塞。
void Redis::observer_channel_message()
{
    while (_running)
    {
        pollfd pfd{};
        pfd.fd = _subcribe_context->fd;
        pfd.events = POLLIN;

        // 如果运行标志被主线程设置为 false，立刻跳出
        if (!_running) break; 

        // 最多等 100ms，没有消息就继续循环
        if (poll(&pfd, 1, 100) <= 0)
        {
            continue;
        }

        // 如果 poll 返回了可读事件，但主线程刚好要退出，这里也要再查一次
        if (!_running) break;


        lock_guard<mutex> lock(_cmdMutex);

        // 如果获取到锁了，主线程可能已经释放了 context，需要做个判空防御
        if (_subcribe_context == nullptr) break;

        redisReply *reply = nullptr;
        if (redisGetReply(this->_subcribe_context, (void **)&reply) != REDIS_OK)
        {
            if (reply != nullptr)
            {
                freeReplyObject(reply);
            }
            continue;
        }

        // 订阅消息格式：["message", "频道", "内容"]
        if (reply != nullptr && reply->element[2] != nullptr && reply->element[2]->str != nullptr)
        {
            _notify_message_handler(atoi(reply->element[1]->str), reply->element[2]->str);
        }

        freeReplyObject(reply);
    }
}

void Redis::init_notify_handler(function<void(int, string)> fn)
{
    this->_notify_message_handler = fn;
}