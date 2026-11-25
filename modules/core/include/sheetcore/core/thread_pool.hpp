#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <future>
#include <mutex>
#include <queue>
#include <thread>
#include <type_traits>
#include <vector>

namespace sheetcore {

class CancellationToken {
public:
    CancellationToken() : cancelled_(std::make_shared<std::atomic<bool>>(false)) {}

    void cancel() noexcept { cancelled_->store(true, std::memory_order_release); }
    bool isCancelled() const noexcept { return cancelled_->load(std::memory_order_acquire); }
    void reset() noexcept { cancelled_->store(false, std::memory_order_release); }

private:
    std::shared_ptr<std::atomic<bool>> cancelled_;
};

class ThreadPool {
public:
    explicit ThreadPool(std::size_t threadCount = 0);
    ~ThreadPool();

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    std::size_t threadCount() const noexcept { return workers_.size(); }

    template <typename F>
    auto submit(F&& fn) -> std::future<std::invoke_result_t<F>> {
        using ReturnT = std::invoke_result_t<F>;
        auto task = std::make_shared<std::packaged_task<ReturnT()>>(std::forward<F>(fn));
        std::future<ReturnT> fut = task->get_future();
        {
            std::lock_guard<std::mutex> lock(mutex_);
            tasks_.push([task]() { (*task)(); });
        }
        cv_.notify_one();
        return fut;
    }

private:
    void workerLoop();

    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> tasks_;
    std::mutex mutex_;
    std::condition_variable cv_;
    bool stopping_ = false;
};

} // namespace sheetcore
