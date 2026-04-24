
#include <string>
#include <memory>
#include <mutex>
using namespace std;

#include <time.h>

#include "Timestamp.hpp"
#include "AppendFile.hpp"

#ifndef LOGFILE_HPP
#define LOGFILE_HPP
namespace tulun
{
    //负责日志文件的自动滚动（rotation）和定期刷新
    class LogFile
    {
    private:
        const std::string basename_;//文件基础命名
        const size_t rollSize_; // 文件大小阈值，超过就回滚

        const int flushInterval_; //刷新间隔
        const int checkEventN_;   //刷新次数（30次）
        int count_;     //当前已写入的日志计数

    private:
        time_t startOfPeriod_;//当前周期开始事件（按天滚动）
        time_t lastRoll_;//上次滚动时间
        time_t lastFlush_;//上次刷新时间
        static const size_t kRollPerSeconds_ = 60 * 60 * 24;//一天的秒数

        //生成日志文件名
        static std::string getLogFileName(const std::string &basename,
                                          const tulun::Timestamp &now);

    private:
        std::unique_ptr<std::mutex> mutex_;       //
        std::unique_ptr<tulun::AppendFile> file_; //当前打开的日志文件

    private:
        //内部写入方法
        void append_unlocked(const char *msg, const size_t len);

    public:
        LogFile(const std::string &basename,//文件名
                size_t rollsize = 1024 * 1024 * 2,//2M 
                int flushInterval = 3,  //刷新时间间隔s
                int checkEventN = 30,   //刷新次数
                bool threadSafe = true);
        ~LogFile();
        LogFile(const LogFile &) = delete;
        LogFile &operator=(const LogFile &) = delete;

        void append(const std::string &msg);
        void append(const char *msg, const size_t len);
        void flush();
        bool rollFile();//滚动文件
    };
} // namespace tulun

#endif