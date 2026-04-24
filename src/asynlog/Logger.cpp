

#include "LogMessage.hpp"
#include "Logger.hpp"
#include <iostream>
#include <stdio.h>
using namespace std;
namespace tulun
{
    void defaultOutput(const std::string &msg)
    {
        size_t n = fwrite(msg.c_str(),sizeof(char),msg.size(),stdout);
    }

    void defaultFlush()
    {
        fflush(stdout);
    }

    typename Logger::OutputFun Logger::s_output_ = defaultOutput;
    typename Logger::FlushFun Logger::s_flush_ = defaultFlush;


    tulun::LOG_LEVEL InitLogLevel()
    {
        if (::getenv("TULUN::LOG_TRACE"))
        {
            return tulun::LOG_LEVEL::TRACE;
        }
        else if (::getenv("TULUN::LOG_DEBUG"))
        {
            return tulun::LOG_LEVEL::DEBUG;
        }
        else
        {
            return tulun::LOG_LEVEL::INFO; // 默认级别
        }
    }

    Logger::Logger(const tulun::LOG_LEVEL &level,
                   const std::string filename,
                   const std::string &funcname,
                   const int line)
        : impl_{level, filename, funcname, line}
    {
    }

    Logger::~Logger() // 将日志写入文件
    {
        impl_ << '\n';
        s_output_(impl_.toString());                        // 调用输出函数
        s_flush_();                                         // 刷新缓存区
        if (impl_.getLogLevel() == tulun::LOG_LEVEL::FATAL) // 特殊处理
        {
            fprintf(stderr, "Process exit \n");
            exit(EXIT_FAILURE); // 致命错误时退出程序
        }
    }

    tulun::LogMessage &Logger::stream()
    {
        return impl_;
    }

    tulun::LOG_LEVEL Logger::s_level_ = InitLogLevel();
    

    tulun::LOG_LEVEL Logger::getLogLevel()
    {
        return s_level_;
    }
    void Logger::setLogLevel(const tulun::LOG_LEVEL &level)
    {
        s_level_ = level;
    }

    void Logger::setOutput(const OutputFun &func) // 设置输出函数
    {
        s_output_ = func;
    }
    void Logger::setFlush(const FlushFun &func) // 设置刷新函数
    {
        s_flush_ = func;
    }

}
