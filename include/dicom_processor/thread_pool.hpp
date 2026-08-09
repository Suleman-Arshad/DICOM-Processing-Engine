#pragma once
#include <atomic>
#include <condition_variable>
#include <functional>
#include <future>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <vector>

namespace dicom
{

    class ThreadPool
    {
    public:
        explicit ThreadPool(size_t numThreads = 0);
        ~ThreadPool();

        ThreadPool(const ThreadPool &) = delete;
        ThreadPool &operator=(const ThreadPool &) = delete;

        template <typename F, typename... Args>
        auto submit(F &&f, Args &&...args) -> std::future<std::invoke_result_t<F, Args...>>
        {
            using ReturnType = std::invoke_result_t<F, Args...>;
            auto boundTask = std::make_shared<std::packaged_task<ReturnType()>>(
                std::bind(std::forward<F>(f), std::forward<Args>(args)...));
            std::future<ReturnType> future = boundTask->get_future();
            {
                std::lock_guard<std::mutex> lock(queueMutex_);
                if (stopping_)
                {
                    throw std::runtime_error("ThreadPool::submit() called after shutdown began");
                }
                tasks_.emplace([boundTask]()
                               { (*boundTask)(); });
            }
            condition_.notify_one();
            return future;
        }

        size_t threadCount() const { return workers_.size(); }

    private:
        void workerLoop();

        std::vector<std::thread> workers_;
        std::queue<std::function<void()>> tasks_;
        std::mutex queueMutex_;
        std::condition_variable condition_;
        std::atomic<bool> stopping_{false};
    };

} // namespace dicom
