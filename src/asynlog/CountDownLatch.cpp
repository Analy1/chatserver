
#include "CountDownLatch.hpp"

namespace tulun
{

    CountDownLatch::CountDownLatch(int count)
        : count_(count)
    {

    }

    void CountDownLatch::wait()
    {
        std::unique_lock<std::mutex> locker(mutex_);
        while (count_ > 0)
        {
            cond_.wait(locker);
        }
    }
    void CountDownLatch::countDown()
    {
        std::unique_lock<std::mutex> locker(mutex_);
        count_ -= 1;
        if(count_ == 0)
        {
            cond_.notify_all();
        }
    }
    int CountDownLatch::getCount() const
    {
        std::unique_lock<std::mutex> locker(mutex_);
        return count_;
    }

}
