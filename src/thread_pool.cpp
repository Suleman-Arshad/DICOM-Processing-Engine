#include "dicom_processor/thread_pool.hpp"
#include <algorithm>

namespace dicom
{

    ThreadPool::ThreadPool(size_t numThreads)
    {
        if (numThreads == 0)
        {
            const unsigned hw = std::thread::hardware_concurrency();
            numThreads = hw > 0 ? hw : 1;
        }
        workers_.reserve(numThreads);
        for (size_t i = 0; i < numThreads; ++i)
        {
            workers_.emplace_back([this]
                                  { workerLoop(); });
        }
    }

    ThreadPool::~ThreadPool()
    {
        {
            std::lock_guard<std::mutex> lock(queueMutex_);
            stopping_ = true;
        }
        condition_.notify_all();
        for (std::thread &worker : workers_)
        {
            if (worker.joinable())
            {
                worker.join();
            }
        }
    }

    void ThreadPool::workerLoop()
    {
        while (true)
        {
            std::function<void()> task;
            {
                std::unique_lock<std::mutex> lock(queueMutex_);
                condition_.wait(lock, [this]
                                { return stopping_ || !tasks_.empty(); });
                if (tasks_.empty())
                {
                    return;
                }
                task = std::move(tasks_.front());
                tasks_.pop();
            }
            task();
        }
    }

} // namespace dicom
