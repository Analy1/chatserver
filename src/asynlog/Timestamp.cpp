#include "Timestamp.hpp"
#include "LogCommon.hpp"
#include <sys/time.h>
#include <time.h>

namespace tulun
{
    Timestamp::Timestamp() : micr_(0) {}
    Timestamp::Timestamp(uint64_t ms) : micr_(ms)
    {
    }

    void Timestamp::swap(Timestamp &other)
    {
        std::swap(this->micr_, other.micr_);
    }

    std::string Timestamp::toString() const
    {
        char buff[SMALL_BUFF_LEN] = {};     // 缓冲区
        time_t sec = micr_ / KMinPerSec;    // 秒
        time_t mic = micr_ % KMinPerSec; // 微妙
        sprintf(buff,"%ld.%ld",sec,mic);
        return std::string(buff);
    }

    std::string Timestamp::toFormattedString(bool showmicro) const
    {
        char buff[SMALL_BUFF_LEN] = {};     // 缓冲区
        time_t sec = micr_ / KMinPerSec;    // 秒
        time_t mic = micr_ % KMinPerSec; // 微妙
        struct tm dtm = {};
        localtime_r(&sec, &dtm); // 将秒数转换成年月日
        int pos = sprintf(buff, "%4d/%02d/%02d-%02d:%02d:%02d",
                          dtm.tm_year + 1900,
                          dtm.tm_mon + 1,
                          dtm.tm_mday,
                          dtm.tm_hour,
                          dtm.tm_min,
                          dtm.tm_sec);
        if(showmicro)
        {
            sprintf(buff + pos,".%ldZ",mic);
        }
        return std::string(buff);
    }

    // 2026/03/28  09:12:24.3245Z
    std::string Timestamp::toFormattedFile() const
    {
        char buff[SMALL_BUFF_LEN] = {};     // 缓冲区
        time_t sec = micr_ / KMinPerSec;    // 秒
        time_t mic = micr_ % KMinPerSec; // 微妙
        struct tm dtm = {};
        localtime_r(&sec, &dtm); // 将秒数转换成年月日
        int pos = sprintf(buff, "%4d%02d%02d-%02d%02d%02d",
                          dtm.tm_year + 1900,
                          dtm.tm_mon + 1,
                          dtm.tm_mday,
                          dtm.tm_hour,
                          dtm.tm_min,
                          dtm.tm_sec);

        sprintf(buff + pos,".%ldZ",mic);
        
        return std::string(buff);
    }

    bool Timestamp::valid() const
    {
        return micr_ > 0;
    }
    u_int64_t Timestamp::getMicro() const
    {
        return micr_;
    }
    u_int64_t Timestamp::getMill() const
    {
        return micr_ / 1000;
    }
    time_t Timestamp::getSecond() const
    {
        return micr_ / KMinPerSec;
    }

    Timestamp Timestamp::Now()
    {
        struct timeval tv;
        gettimeofday(&tv, nullptr);
        return Timestamp(tv.tv_sec * KMinPerSec + tv.tv_usec); // 把所有时间都换算成微秒存储进对象里
    }
    Timestamp Timestamp::Invalid()
    {
        return Timestamp{};
    }
    // static const int KMinPerSec = 1000 * 1000;

}
