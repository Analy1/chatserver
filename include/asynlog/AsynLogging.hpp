
#include <thread>
#include <atomic>
#include <string>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <deque>
#include <vector>

#include "AppendFile.hpp"
#include "LogFile.hpp"
#include "CountDownLatch.hpp"
using namespace std;

#ifndef ASYN_LOGGING_HPP
#define ASYN_LOGGING_HPP

namespace tulun
{
    class AsynLogging
    {
    private:
        // 后端消费
        void workthreadfunc();

    private:
        const int flushInterval_;              // 后台线程每隔3秒强制刷一次盘
        std::atomic<bool> running_;            // 控制后台线程该继续跑还是该停了
        const std::string basename_;           // 日志文件名基础
        const size_t rollSize_;                // 滚动大小，传给内部LogFile
        tulun::CountDownLatch latch_;          //门闩
        std::unique_ptr<std::thread> pthread_; // 后台线程对象，负责消费队列里的日志

        std::mutex mutex_;
        std::condition_variable cond_;

        std::string currentBuffer_;       // 当前正在写的缓冲区
        //std::deque<std::string> buffers_; // 待写入队列，存满了的currentBuffer_，等待后台线程消费
        std::vector<std::string> buffers_;

        tulun::LogFile output_; // 文件写入器，后台线程把队列里的数据交给他

    public:
        AsynLogging(const std::string &basename,
                    const size_t rollsize = 1024 * 1024 * 2,
                    int flushInterval = 3);
        ~AsynLogging();

        AsynLogging(const AsynLogging &) = delete;
        AsynLogging &operator=(const AsynLogging &) = delete;

        // 前端写入
        void append(const std::string &msg);
        void append(const char *msg, const size_t len);

        // 线程启动与停止
        void start();
        void stop();

        void flush();
    };
}

#endif
