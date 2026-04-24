

#include "LogCommon.hpp"
#include "LogMessage.hpp"
#include "Timestamp.hpp"
#include <sys/syscall.h>
// #include<pthread.h>
#include <unistd.h>
#include <string>
using namespace std;

namespace tulun
{

    // 级别 + 文件名 + 函数名 + 行号
    LogMessage::LogMessage(const tulun::LOG_LEVEL &level,
                           const std::string filename,
                           const std::string &funcname,
                           const int line)
        : level_(level)
    {
        pid_t tid = syscall(SYS_gettid);

        std::ostringstream oss;
        oss << tulun::Timestamp::Now().toFormattedString() << " ";
        oss << ::to_string(tid) << " ";
        oss << tulun::LLTOSTR[static_cast<int>(level_)] << " ";
        int pos = filename.find_last_of('/');
        std::string filestr = filename.substr(pos + 1);
        oss << filestr << " " << funcname << " " << line << " ";
        header_ = oss.str();
    }

    const tulun::LOG_LEVEL &LogMessage::getLogLevel() const
    {
        return level_;
    }
    const std::string LogMessage::toString() const
    {
        return header_ + text_;
    }

}
