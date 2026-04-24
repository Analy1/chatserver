#ifndef LOG_COMMON_HPP
#define LOG_COMMON_HPP

namespace tulun
{
    //缓冲区大小定义
    static const int SMALL_BUFF_LEN = 128;
    static const int MEDIAN_BUFF_LEN = 512;
    static const int LARGE_BUFF_LEN = 1024;

    enum LOG_LEVEL
    {
        TRACE = 0,//跟踪
        DEBUG,  //调试
        INFO,   //信息
        WARN,   //警告
        ERROR,  //错误
        FATAL,  //知名
        MUN_LOG_LEELS,  //边界标记，不是真正的日志级别
    };

    static const char* LLTOSTR[]=
    {
        "TRACE",
        "DEBUG",
        "INFO",
        "WARN",
        "ERROR",
        "FATAL",
        "MUN_LOG_LEELS",
    };
}

#endif