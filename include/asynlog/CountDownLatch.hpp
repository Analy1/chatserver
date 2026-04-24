
#include <mutex>
#include <condition_variable>

#ifndef COUNTDOWNLATCH_HPP
#define COUNTDOWNLATCH_HPP

namespace tulun
{
    class CountDownLatch
    {
    private:
        int count_;//倒计时计数器，初始值为N，每调用一次countDown() 减1
        mutable std::mutex mutex_;
        std::condition_variable cond_;

    public:
        //设置初始计数器，表示需要等待count个事件完成
        CountDownLatch(int count);
        ~CountDownLatch() = default;

        //阻塞等待
        //如果count_ > 0 ，调用线程无限期阻塞
        //直到count_ == 0 ，才被唤醒继续执行
        void wait();

        void countDown();//倒数器
        int getCount() const;//查询剩余计数
    };
}

#endif