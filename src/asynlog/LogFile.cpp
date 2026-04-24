
#include <unistd.h>

#include "LogCommon.hpp"
#include "Timestamp.hpp"
#include "LogFile.hpp"

namespace tulun
{

    //获取主机名和进程ID
    const std::string hostname()
    {
        char buff[SMALL_BUFF_LEN] = {};
        //系统调用：获取本机主机名
        if (!::gethostname(buff, SMALL_BUFF_LEN))
        {
            return std::string(buff);
        }
        else
        {
            return std::string("nuknownhost");
        }
    }
    pid_t pid()
    {
        return ::getpid();
    }

    //生成日志文件名：yhping.20260421.143025.123456Z.server01.12345.log
    //基础名 + 年月日 + 时分秒 + 微妙 + 主机名 + 进程ID
    std::string LogFile::getLogFileName(const std::string &basename,
                                        const tulun::Timestamp &now)
    {
        std::string filename;
        filename.reserve(basename.size() + SMALL_BUFF_LEN);
        filename = basename; // yhping
        filename += ".";
        filename += now.toFormattedFile(); // yhping.20260408.162334.2345Z
        filename += ".";                   // yhping.20260408.162334.2345Z.
        filename += hostname();
        filename += ".";
        filename += std::to_string(pid());
        filename += ".log";
        return filename;
    }

    //内部写入方法，把数据往文件里写
    void LogFile::append_unlocked(const char *msg, const size_t len)
    {
        //1.实际写入文件
        file_->append(msg, len);
        //2.检查是否需要回滚
        if (file_->getWriteBytes() > rollSize_)
        {
            rollFile();//回滚
        }
        else
        {
            count_ += 1;//计数器+1

            //3.每checkEventN_条日志检查一次时间和刷新
            if (count_ > checkEventN_)
            {
                count_ = 0;
                time_t now = ::time(nullptr);

                // 4. 计算当前时间所属的周期起点（当天0点）
                //    例如：now = 2026-04-21 14:30:25
                //    kRollPerSeconds_ = 86400（一天的秒数）
                //    thisPeriod = (now / 86400) * 86400 = 当天0点的时间戳
                time_t thisPeriod = (now / kRollPerSeconds_) * kRollPerSeconds_;

                //5.检查是否跨天了
                if (thisPeriod != startOfPeriod_)
                {
                    rollFile();//跨天了，滚动到新文件
                }
                //6.检查是否需要刷新（距离赏赐刷新超过flushInterval_ 秒）
                else if (now - lastFlush_ > flushInterval_)
                {
                    lastFlush_ = now;
                    file_->flush();
                }
            }
        }
    }


    LogFile::LogFile(const std::string &basename,
                     size_t rollsize,
                     int flushInterval,
                     int checkEventN,
                     bool threadSafe)
        : basename_(basename),
          rollSize_(rollsize),
          flushInterval_(flushInterval),
          checkEventN_(checkEventN),
          mutex_{threadSafe ? new std::mutex{} : nullptr},
          count_(0),
          startOfPeriod_(0),
          lastRoll_(0),
          lastFlush_(0),
          file_{nullptr}
    {
        rollFile();
    }
    LogFile::~LogFile()
    {
    }

    void LogFile::append(const std::string &msg)
    {
        append(msg.c_str(), msg.size());
    }

    //公开的写入接口
    void LogFile::append(const char *msg, const size_t len)
    {
        if (mutex_.operator bool())
        {
            std::unique_lock<std::mutex> locker(*mutex_);
            append_unlocked(msg, len);
        }
        else
        {
            append_unlocked(msg, len);
        }
    }
    void LogFile::flush()
    {
        file_->flush();
    }

    //滚动文件
    bool LogFile::rollFile()
    {
        //1.获取当前时间
        tulun::Timestamp now;
        now = tulun::Timestamp::Now();

        //2.生成新文件名
        std::string filename = getLogFileName(basename_, now);

        //3.今天的开始时间（0点）
        time_t start = (now.getSecond() / kRollPerSeconds_) * kRollPerSeconds_;

        // 2026 4 8 16 25 40 // start 2024 4 8 0 0 0
        //4.防止同一秒内重复滚动
        if (now.getSecond() > lastRoll_)
        {
            lastRoll_ = now.getSecond();//更新上次滚动时间
            lastFlush_ = now.getSecond();//更新上次刷新时间
            startOfPeriod_ = start;//记录周期起点

            //5.创建新的AppendFile对象（旧对象自动销毁，文件自动关闭）
            file_.reset(new tulun::AppendFile(filename));
            return true;
        }
        return false;
    }

} // namespace tulun