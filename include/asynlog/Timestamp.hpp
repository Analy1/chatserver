#include<string>
#include<time.h>
#include<stdint.h>


using namespace std;

#ifndef TIMESTAMP_HPP
#define TIMESTAMP_HPP

namespace tulun
{
    class Timestamp
    {
    private:
        uint64_t micr_;//微妙
    public:
        Timestamp();//默认构造。创建一个无效的时间戳
        Timestamp(uint64_t ms);//带参构造。直接用微秒数初始化时间戳。

        Timestamp(const Timestamp& ) = default;//拷贝构造。用另一个时间戳对象创建副本。
        Timestamp &operator=(const Timestamp&) = default;

        //交换两个时间戳对象的内部微秒值
        void swap(Timestamp &other);

        //直接返回内部存储的微秒数字符串。
        std::string toString() const;

        //转为可读的日期时间字符串。若参数为 true，显示微秒；否则只显示到秒。
        std::string toFormattedString(bool showmicro = true) const;

        //转为适合做文件名的字符串。
        std::string toFormattedFile() const;

        bool valid() const;//判断时间戳是否有效（即内部微秒数是否大于 0）
        u_int64_t getMicro() const;//获取原始微秒数。
        u_int64_t getMill() const;//获取换算后的毫秒数（即 micr_ / 1000）。
        time_t getSecond() const;//获取 Unix 时间戳（秒级）。

    public:
        static Timestamp Now();//获取当前时间。
        static Timestamp Invalid();//生成无效对象。返回一个 micr_ 为 0 的时间戳，用于错误处理。
        static const int KMinPerSec = 1000 * 1000;//用于秒到微秒的换算计算。
    };

    //时差计算函数
    inline time_t diffSecond(const Timestamp &a,const Timestamp &b)
    {
        return a.getSecond() - b.getSecond();
    }
    inline time_t diffMicro(const Timestamp &a,const Timestamp &b)
    {
        return a.getMicro() - b.getMicro();
    }

    //时间加法函数
    inline Timestamp addTimeSec(const Timestamp &a,const u_int64_t sec)
    {
        return Timestamp(a.getMicro() + sec * Timestamp::KMinPerSec);
    }
    inline Timestamp addTimeMilloc(const Timestamp &a,const u_int64_t mc)
    {
        return Timestamp(a.getMicro() + mc * 1000);
    }
    inline Timestamp addTimeMicro(const Timestamp &a,const u_int64_t mc)
    {
        return Timestamp(a.getMicro() + mc);
    }
}

#endif