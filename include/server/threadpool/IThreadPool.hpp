#ifndef ITHREADPOOL_HPP
#define ITHREADPOOL_HPP

#include <functional>
#include <memory>
#include <future>

namespace tulun
{
    // 线程池统一抽象接口
    // 所有线程池类型（固定/缓存/工作窃取）都实现此接口，
    // 上层业务只依赖 IThreadPool，切换类型改工厂即可
    class IThreadPool
    {
    public:
        virtual ~IThreadPool() = default;

        // 停止线程池，等待所有任务完成后退出
        virtual void Stop() = 0;

        // 提交任务并返回 future，用于获取异步结果
        // 示例: auto future = pool->submit([](int x) { return x * 2; }, 3);
        //       int result = future.get();   // result = 6
        template <typename Func, typename... Args>
        auto submit(Func &&func, Args &&...args)
        {
            using RetType = decltype(func(std::forward<Args>(args)...));

            auto task = std::make_shared<std::packaged_task<RetType()>>(
                std::bind(std::forward<Func>(func), std::forward<Args>(args)...));

            std::future<RetType> result = task->get_future();
            Enqueue([task]() { (*task)(); });
            return result;
        }

    public:
        // 提交任务（不关心返回值），Fire-and-forget 调用
        void AddTask(std::function<void()> task)
        {
            Enqueue(std::move(task));
        }

    protected:
        // 子类实现：将任务放入队列
        virtual void Enqueue(std::function<void()> task) = 0;
    };

} // namespace tulun

#endif
