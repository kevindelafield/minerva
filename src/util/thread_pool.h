#pragma once

#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <functional>
#include <atomic>
#include <future>

namespace minerva
{

    class thread_pool
    {
    public:
        typedef std::function<void()> work_element;

        /**
         * Create a thread pool with the specified number of worker threads.
         * @param count Number of worker threads (must be > 0)
         */
        explicit thread_pool(int count);
        
        // Non-copyable and non-movable due to atomic and const members
        thread_pool(const thread_pool&) = delete;
        thread_pool& operator=(const thread_pool&) = delete;
        thread_pool(thread_pool&&) = delete;
        thread_pool& operator=(thread_pool&&) = delete;

        /**
         * Destructor automatically stops the thread pool and waits for completion.
         * Equivalent to calling stop() followed by wait().
         */
        ~thread_pool();

        /**
         * Queue a work item for execution.
         * Thread-safe and can be called from any thread.
         * 
         * @param work The work item to queue
         * @return true if work was successfully queued, false if thread pool is stopped or stopping
         */
        bool queue_work_item(work_element work);

        /**
         * Submit a task and get a future for the result.
         * Template function for type-safe async execution.
         * 
         * @param f Function or callable to execute
         * @param args Arguments to pass to the function
         * @return Future containing the result of the function
         * @throws std::runtime_error if thread pool is stopped or stopping
         * 
         * Note: Unlike queue_work_item(), this method throws on failure because
         * returning an invalid future would be more dangerous than an exception.
         */
        template<typename F, typename... Args>
        auto submit(F&& f, Args&&... args) -> std::future<typename std::result_of<F(Args...)>::type>;

        /**
         * Batch work item operations for efficiency.
         * Call begin_queue_work_item(), multiple queue_work_item_batch(), then end_queue_work_item().
         * 
         * @return true if batch mode started successfully, false if thread pool is stopped or stopping
         */
        bool begin_queue_work_item();
        
        /**
         * Queue a work item in batch mode (between begin_queue_work_item and end_queue_work_item).
         * 
         * @param work The work item to queue
         * @return true if work was successfully queued, false if thread pool is stopped or stopping
         */
        bool queue_work_item_batch(work_element work);
        
        /**
         * End batch queuing mode and notify workers.
         */
        void end_queue_work_item();

        /**
         * Start the thread pool. Must be called before queueing work.
         */
        void start();
        
        /**
         * Signal the thread pool to begin shutdown.
         * This will stop accepting new work and signal workers to exit after completing current tasks.
         * Call wait() to block until all threads have actually exited.
         * 
         * This method is idempotent - safe to call multiple times.
         * Subsequent calls after the first will have no effect.
         */
        void stop();

        /**
         * Wait for all worker threads to exit.
         * Must call stop() first to trigger shutdown.
         * 
         * This method is idempotent - safe to call multiple times.
         * If already waited, subsequent calls return immediately.
         */
        void wait();

        /**
         * Convenience method that calls stop() followed by wait().
         * Equivalent to the old stop_and_wait() behavior.
         */
        void stop_and_wait() {
            stop();
            wait();
        }

        /**
         * Get the number of worker threads.
         */
        int get_thread_count() const { return thread_count; }
        
        /**
         * Get the number of pending work items.
         */
        size_t get_queue_size() const;

        /**
         * Check if the thread pool is running.
         */
        bool is_running() const { return running.load(); }

    private:
        const int thread_count;
        std::atomic<bool> should_shutdown{false};
        std::atomic<bool> running{false};
        
        std::queue<work_element> work_items;
        std::vector<std::thread> threads;  // Use vector instead of set of pointers
        
        mutable std::mutex work_mutex;
        std::condition_variable work_condition;
        
        void worker_thread();
    };

    // Template implementation
    template<typename F, typename... Args>
    auto thread_pool::submit(F&& f, Args&&... args) -> std::future<typename std::result_of<F(Args...)>::type>
    {
        using return_type = typename std::result_of<F(Args...)>::type;
        
        auto task = std::make_shared<std::packaged_task<return_type()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...)
        );
        
        std::future<return_type> result = task->get_future();
        
        bool queued = queue_work_item([task]() {
            (*task)();
        });
        
        if (!queued) {
            // If we couldn't queue the work, we need to handle this
            // The future will never be satisfied, so we should throw
            throw std::runtime_error("Cannot submit work to stopped thread pool");
        }
        
        return result;
    }
}
