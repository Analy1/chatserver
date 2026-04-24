
#include <string>
#include<sstream>
#include "LogCommon.hpp"
using namespace std;

#ifndef LOG_MESSAGE_HPP
#define LOG_MESSAGE_HPP

namespace tulun
{
    class LogMessage
    {
    private:
        std::string header_;
        std::string text_;
        tulun::LOG_LEVEL level_;

    public:
        //级别 + 文件名 + 函数名 + 行号 
        LogMessage(const tulun::LOG_LEVEL &level,
                   const std::string filename,
                   const std::string &funcname,
                   const int line);

        LogMessage(const LogMessage &) = default;
        LogMessage &operator=(const LogMessage &) = default;
        ~LogMessage() = default;

        const tulun::LOG_LEVEL & getLogLevel() const;
        const std::string toString() const;

        template<class _Ty>
        LogMessage& operator<<(const _Ty &text)
        {
            std::ostringstream oss;
            oss << ":" << text;
            text_ += oss.str();//追加到日志后
            return *this;
        }
        
    };
}

#endif
