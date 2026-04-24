

#include "LogMessage.hpp"
#include<functional>

using namespace std;
#ifndef LOGGER_HPP
#define LOGGER_HPP

namespace tulun
{
    class Logger
    {
    public:
        using OutputFun = std::function<void(const std::string &)>;
        using FlushFun = std::function<void(void)>;
    private:
        static OutputFun s_output_;//输出函数
        static FlushFun s_flush_;;//刷新函数
    public:
        static void setOutput(const OutputFun &);//设置输出函数
        static void setFlush(const FlushFun &);//设置刷新函数

    private:
        tulun::LogMessage impl_; // 内部持有日志消息对象

    public:
        // 构造时：创建日志消息对象
        Logger(const tulun::LOG_LEVEL &level,
               const std::string filename,
               const std::string &funcname,
               const int line);
        ~Logger(); // 将日志写入文件

        tulun::LogMessage &stream(); // 返回消息对象的引用，用于链式输出

    private:
        static tulun::LOG_LEVEL s_level_;

    public:
        static tulun::LOG_LEVEL getLogLevel();
        static void setLogLevel(const tulun::LOG_LEVEL &level);
    };

#define LOG_TRACE                                                \
    if (tulun::Logger::getLogLevel() <= tulun::LOG_LEVEL::TRACE) \
    tulun::Logger(tulun::LOG_LEVEL::TRACE, __FILE__, __func__, __LINE__).stream()

#define LOG_DEBUG                                                \
    if (tulun::Logger::getLogLevel() <= tulun::LOG_LEVEL::DEBUG) \
    tulun::Logger(tulun::LOG_LEVEL::DEBUG, __FILE__, __func__, __LINE__).stream()

#define LOG_INFO                                                \
    if (tulun::Logger::getLogLevel() <= tulun::LOG_LEVEL::INFO) \
    tulun::Logger(tulun::LOG_LEVEL::INFO, __FILE__, __func__, __LINE__).stream()

#define LOG_WARN                                                \
    if (tulun::Logger::getLogLevel() <= tulun::LOG_LEVEL::WARN) \
    tulun::Logger(tulun::LOG_LEVEL::WARN, __FILE__, __func__, __LINE__).stream()

#define LOG_ERROR \
    tulun::Logger(tulun::LOG_LEVEL::ERROR, __FILE__, __func__, __LINE__).stream()

#define LOG_FATAL \
    tulun::Logger(tulun::LOG_LEVEL::FATAL, __FILE__, __func__, __LINE__).stream()

#define LOG_SYSERR \
    tulun::Logger(tulun::LOG_LEVEL::ERROR, __FILE__, __func__, __LINE__).stream()

#define LOG_SYSFATAL \
    tulun::Logger(tulun::LOG_LEVEL::FATAL, __FILE__, __func__, __LINE__).stream()
}

#endif