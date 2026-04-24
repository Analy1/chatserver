

#include <stdio.h>
#include <string>
#include <memory>
#include<error.h>
#include<string.h>
#include "AppendFile.hpp"
namespace tulun
{
    size_t AppendFile::write(const char *msg, const size_t len)
    {
        // fwrite多线程安全，fwrite_unlocked多线程不安全
        //return fwrite_unlocked(msg, sizeof(char), len, fp_);//加入门闩，也OK了
        return fwrite(msg, sizeof(char), len, fp_);//修改了这里输出就OK了
    }

    AppendFile::AppendFile(const std::string &logfilename)
        : buffer_{nullptr}, fp_(nullptr), writeBytes_{0}
    {
        buffer_.reset(new (std::nothrow) char[FILE_BUFF_SIZE]);
        if (!buffer_)
        {
            fprintf(stderr, "new error\n");
            exit(EXIT_FAILURE);
        }
        fp_ = fopen(logfilename.c_str(), "a");
        if (nullptr == fp_)
        {
            fprintf(stderr, "fopen error\n");
            exit(EXIT_FAILURE);
        }
        setbuffer(fp_, buffer_.get(), FILE_BUFF_SIZE);
    }
    AppendFile::~AppendFile()
    {
        fclose(fp_);
        fp_ = nullptr;
        buffer_.reset();
    }

    void AppendFile::append(const std::string &msg)
    {
        append(msg.c_str(), msg.size()); // 调用下面的
    }
    void AppendFile::append(const char *msg, const size_t len)
    {
        size_t n = write(msg, len);
        size_t remain = len - n; // 还有剩余
        while (remain > 0)
        {
            size_t x = write(msg + n, remain); // 写剩下的
            if(x == 0)
            {
                int err = ferror(fp_);
                if(err)
                {
                    fprintf(stderr,"appendFile::append failed %s \n",strerror(err));
                    break;
                }
            }
            n += x;
            remain = len - n;
        }
        writeBytes_ += n;

    }
    void AppendFile::flush()
    {
        fflush(fp_);
    }
    size_t AppendFile::getWriteBytes() const
    {
        return writeBytes_;
    }

}
