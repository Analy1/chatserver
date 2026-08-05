#ifndef THREADPOOLFACTORY_HPP
#define THREADPOOLFACTORY_HPP

#include "FixedThreadPool.hpp"
#include "CachedThreadPool.hpp"
#include "WorkStealingThreadPool.hpp"
#include <memory>
#include <thread>

namespace tulun
{
    // 线程池类型枚举
    enum class PoolType
    {
        Fixed,          // 固定线程数，任务粒度均匀时最优
        Cached,         // 弹性伸缩，适合突发流量
        WorkStealing    // 工作窃取，适合任务粒度差异大
    };

    // 线程池配置参数
    struct ThreadPoolConfig
    {
        size_t queueSize     = 1024;                                  // 任务队列容量
        int    coreThreads   = static_cast<int>(std::thread::hardware_concurrency());  // 核心/固定线程数
        int    maxThreads    = static_cast<int>(std::thread::hardware_concurrency());  // 最大线程数（仅 Cached 使用）
    };

    // 工厂函数：根据类型创建线程池
    inline std::unique_ptr<IThreadPool> CreateThreadPool(PoolType type, const ThreadPoolConfig &cfg = {})
    {
        switch (type)
        {
        case PoolType::Fixed:
            return std::make_unique<FixedThreadPool>(cfg.queueSize, cfg.coreThreads);
        case PoolType::Cached:
            return std::make_unique<CachedThreadPool>(cfg.coreThreads, static_cast<int>(cfg.queueSize));
        case PoolType::WorkStealing:
            return std::make_unique<WorkStealingThreadPool>(cfg.queueSize, static_cast<size_t>(cfg.coreThreads));
        default:
            return std::make_unique<FixedThreadPool>(cfg.queueSize, cfg.coreThreads);
        }
    }

} // namespace tulun

#endif
