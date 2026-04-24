

#include "AsynLogging.hpp"

using namespace std;

namespace tulun
{
    const size_t BufMaxLen = 1024 * 8; // 4KB,单个缓冲区大小
    const size_t BufQueueSize = 32;    // 队列最大长度

    void AsynLogging::workthreadfunc()
    {
        // 1.本地临时队列：用于批量写入，避免长时间持锁
        //std::deque<std::string> buffersToWrite;
        std::vector<std::string> buffersToWrite;
        latch_.countDown();
        while (running_)
        {
            {
                // 2.加锁，访问共享队列
                std::unique_lock<std::mutex> locker(mutex_);

                // 3.等待数据到来（或超时1秒）
                while (buffers_.empty() && running_)
                {
                    // 即使队列空，也会每1秒醒来一次
                    cond_.wait_for(locker, std::chrono::seconds(1));
                }

                // 4.把当前正在填的缓冲区也拿走
                buffers_.push_back(std::move(currentBuffer_));

                // 5.重新分配一个新的当前缓冲区
                currentBuffer_.reserve(BufMaxLen);

                // 6.交换，把共享队列的所有数据转移到本地临时队列
                buffersToWrite.swap(buffers_);
                buffers_.reserve(BufQueueSize);
            } //锁释放，后续写入完全不会阻塞前端

            //队列异常时，只保留前2（经验值，可改）个日志，其余的丢弃
            if(buffersToWrite.size() > 50)
            {
                fprintf(stderr,"Dropped log message at larger buffers \n");
                buffersToWrite.erase(buffersToWrite.begin() + 2,buffersToWrite.end());
            }

            // 8.批量写入
            for (const auto &buf : buffersToWrite)
            {
                output_.append(buf);
            }
            // 9.清空临时队列，准备下一轮
            buffersToWrite.clear();
        } 
    }

    AsynLogging::AsynLogging(const std::string &basename,
                             const size_t rollsize,
                             int flushInterval)
        : basename_(basename),
          flushInterval_(flushInterval),
          rollSize_(rollsize),
          running_(false),
          pthread_(nullptr),
          mutex_{},
          cond_{},
          output_(basename, rollsize, flushInterval),
          latch_{1}

    {
        currentBuffer_.reserve(BufMaxLen); // 预分配4KBritish空间，避免频繁扩容
        buffers_.reserve(BufQueueSize);
    }
    
    AsynLogging::~AsynLogging() 
    {
        if(running_)
        {
            stop();
        }
    }

    void AsynLogging::append(const std::string &msg)
    {
        append(msg.c_str(), msg.size());
    }

    void AsynLogging::append(const char *msg, const size_t len)
    {
        // 1.加锁
        std::unique_lock<std::mutex> locker(mutex_);

        // 2.判断当前缓冲区是否装的下这条日志
        if (currentBuffer_.size() >= BufMaxLen ||
            currentBuffer_.capacity() - currentBuffer_.size() < len)
        {
            // 装不下：把满的缓冲区人扔进队列
            buffers_.push_back(std::move(currentBuffer_));
            // 重新分配一个新的当前缓冲区
            currentBuffer_.reserve(BufMaxLen);
        }
        // 3.追加日志到当前缓冲区
        currentBuffer_.append(msg, len); // 如果这里拷贝内容很大，线程函数会被阻塞
        // 4.唤醒后台线程
        cond_.notify_all();
    }

    void AsynLogging::start()
    {
        running_ = true;
        pthread_.reset(new std::thread(&AsynLogging::workthreadfunc, this));
        latch_.wait();//等待后台线程确认就绪后返回
    }

    void AsynLogging::stop()
    {
        running_ = false;
        cond_.notify_all();
        pthread_->join();
    }

    void AsynLogging::flush()
    {
        //std::deque<std::string> bufferToWriter;
        std::vector<std::string> bufferToWriter;
        std::unique_lock<std::mutex> locker(mutex_);
        buffers_.push_back(std::move(currentBuffer_));
        currentBuffer_.reserve(BufMaxLen);
        bufferToWriter.swap(buffers_);
        buffers_.reserve(BufQueueSize);

        for(const auto &buf : bufferToWriter)
        {
            output_.append(buf);
        }
        output_.flush();
        bufferToWriter.clear();
    }

}
