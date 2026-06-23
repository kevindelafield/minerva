#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <future>
#include <mutex>
#include <queue>
#include <thread>
#include <type_traits>
#include <vector>

namespace minerva
{
    /**
     * Fixed-size worker pool that runs std::function<void()> jobs.
     *
     * Lifecycle: STOPPED -> start() -> RUNNING -> stop() -> STOPPING -> wait()
     * -> STOPPED. start() and stop() are idempotent and race-safe via a
     * compare_exchange on an internal state enum.
     */
    class thread_pool
    {
    public:
        typedef std::function<void()> work_element;

        explicit thread_pool(int count);

        thread_pool(const thread_pool&)            = delete;
        thread_pool& operator=(const thread_pool&) = delete;
        thread_pool(thread_pool&&)                 = delete;
        thread_pool& operator=(thread_pool&&)      = delete;

        ~thread_pool();

        /**
         * Queue a single work item. Returns false if the pool is not RUNNING
         * or has begun shutting down.
         */
        bool queue_work_item(work_element work);

        /**
         * Submit a callable and get a future for its result. Throws
         * std::runtime_error if the pool can't accept work.
         */
        template<typename F, typename... Args>
        auto submit(F&& f, Args&&... args)
            -> std::future<std::invoke_result_t<F, Args...>>;

        /**
         * Batch enqueue API. Pattern:
         *     pool.begin_queue_work_item();
         *     pool.queue_work_item_batch(job1);
         *     pool.queue_work_item_batch(job2);
         *     pool.end_queue_work_item();
         *
         * begin always takes the work mutex (so end can always unlock it
         * unconditionally). The boolean returns indicate whether the items
         * were actually accepted into the queue; callers can ignore them
         * and still call end safely.
         */
        bool begin_queue_work_item();
        bool queue_work_item_batch(work_element work);
        void end_queue_work_item();

        void start();
        void stop();
        void wait();
        void stop_and_wait() { stop(); wait(); }

        int    get_thread_count() const { return thread_count; }
        size_t get_queue_size()   const;

        /** True only when started and not shutting down. */
        bool is_running() const
        {
            return state.load() == RUNNING && !should_shutdown.load();
        }

    private:
        enum state_t { STOPPED, RUNNING, STOPPING };

        const int                 thread_count;
        std::atomic<state_t>      state{STOPPED};
        std::atomic<bool>         should_shutdown{false};

        std::queue<work_element>  work_items;
        std::vector<std::thread>  threads;

        mutable std::mutex        work_mutex;
        std::condition_variable   work_condition;

        void worker_thread();
    };

    template<typename F, typename... Args>
    auto thread_pool::submit(F&& f, Args&&... args)
        -> std::future<std::invoke_result_t<F, Args...>>
    {
        using return_type = std::invoke_result_t<F, Args...>;

        auto task = std::make_shared<std::packaged_task<return_type()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...));
        std::future<return_type> result = task->get_future();

        if (!queue_work_item([task]() { (*task)(); }))
        {
            throw std::runtime_error("Cannot submit work to stopped thread pool");
        }
        return result;
    }
}
