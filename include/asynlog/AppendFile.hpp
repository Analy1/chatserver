

#include <stdio.h>
#include <string>
#include <memory>

using namespace std;
#ifndef APPEND_FILE_HPP
#define APPEND_FILE_HPP

namespace tulun
{

    //带缓冲区的文件追加写入器
    class AppendFile
    {
    private:
        static const size_t FILE_BUFF_SIZE = 1024 * 128; // 缓冲区大小
        std::unique_ptr<char[]> buffer_;//智能指针管理的缓冲区
        FILE* fp_;
        size_t writeBytes_;//已写入字节数
        size_t write(const char *msg, const size_t len);//写入方法

    public:
        AppendFile(const std::string &logfilename);
        ~AppendFile();
        AppendFile(const AppendFile &) = delete;
        AppendFile &operator=(const AppendFile &) = delete;

        //追加日志
        void append(const std::string &msg);
        void append(const char *msg, const size_t len);

        void flush();
        size_t getWriteBytes() const;//获取写入子节数
    };
}

#endif
