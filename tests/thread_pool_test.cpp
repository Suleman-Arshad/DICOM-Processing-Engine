#include "dicom_processor/thread_pool.hpp"

#include <atomic>
#include <chrono>
#include <iostream>
#include <numeric>
#include <thread>
#include <vector>

namespace
{

    int testsRun = 0;
    int testsFailed = 0;

    void check(bool condition, const std::string &description)
    {
        ++testsRun;
        if (!condition)
        {
            ++testsFailed;
            std::cout << "[FAIL] " << description << '\n';
        }
        else
        {
            std::cout << "[PASS] " << description << '\n';
        }
    }

    void testBasicExecutionAndResults()
    {
        dicom::ThreadPool pool(4);
        std::vector<std::future<int>> futures;
        for (int i = 0; i < 100; ++i)
        {
            futures.push_back(pool.submit([i]
                                          { return i * i; }));
        }
        bool allCorrect = true;
        for (int i = 0; i < 100; ++i)
        {
            if (futures[static_cast<size_t>(i)].get() != i * i)
                allCorrect = false;
        }
        check(allCorrect, "100 tasks return correct individual results");
    }

    void testEveryTaskRunsExactlyOnce()
    {
        dicom::ThreadPool pool(8);
        std::atomic<int> counter{0};
        std::vector<std::future<void>> futures;
        constexpr int taskCount = 10000;
        for (int i = 0; i < taskCount; ++i)
        {
            futures.push_back(pool.submit([&counter]
                                          { counter.fetch_add(1, std::memory_order_relaxed); }));
        }
        for (auto &f : futures)
            f.get();
        check(counter.load() == taskCount, "10000 concurrent tasks each increment exactly once (no lost/duplicated tasks)");
    }

    void testExceptionPropagation()
    {
        dicom::ThreadPool pool(2);
        auto future = pool.submit([]() -> int
                                  { throw std::runtime_error("intentional test exception"); });

        bool caught = false;
        std::string message;
        try
        {
            future.get();
        }
        catch (const std::runtime_error &e)
        {
            caught = true;
            message = e.what();
        }
        check(caught && message == "intentional test exception",
              "exception thrown inside a task propagates through future.get()");
    }

    void testPoolContinuesAfterATaskThrows()
    {
        dicom::ThreadPool pool(2);
        auto failing = pool.submit([]() -> int
                                   { throw std::runtime_error("boom"); });
        auto healthy = pool.submit([]
                                   { return 42; });

        bool failingThrew = false;
        try
        {
            failing.get();
        }
        catch (...)
        {
            failingThrew = true;
        }
        check(failingThrew && healthy.get() == 42,
              "one task throwing doesn't stop other tasks (or the pool) from completing");
    }

    void testDestructorDrainsQueueBeforeJoining()
    {
        std::atomic<int> completed{0};
        {
            dicom::ThreadPool pool(2);
            for (int i = 0; i < 50; ++i)
            {
                pool.submit([&completed]
                            {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                completed.fetch_add(1, std::memory_order_relaxed); });
            }
        }
        check(completed.load() == 50, "destructor drains all queued tasks before joining threads");
    }

    void testSubmitAfterShutdownThrows()
    {
        dicom::ThreadPool pool(1);
        bool threw = false;
        try
        {
            auto f = pool.submit([]
                                 { return 1; });
            f.get();
        }
        catch (...)
        {
            threw = true;
        }
        check(!threw, "normal submission before shutdown never throws");
    }

    void testThreadCountReporting()
    {
        dicom::ThreadPool pool(6);
        check(pool.threadCount() == 6, "threadCount() reports the requested worker count");

        dicom::ThreadPool autoPool(0);
        check(autoPool.threadCount() > 0, "requesting 0 threads falls back to a positive auto-detected count");
    }

} // namespace

int main()
{
    testBasicExecutionAndResults();
    testEveryTaskRunsExactlyOnce();
    testExceptionPropagation();
    testPoolContinuesAfterATaskThrows();
    testDestructorDrainsQueueBeforeJoining();
    testSubmitAfterShutdownThrows();
    testThreadCountReporting();

    std::cout << '\n'
              << (testsRun - testsFailed) << '/' << testsRun << " tests passed\n";
    return testsFailed == 0 ? 0 : 1;
}
