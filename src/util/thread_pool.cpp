#include "thread_pool.h"
#include "log.h"
#include <cassert>

namespace minerva
{

    thread_pool::thread_pool(int count) 
        : thread_count(count)
    {
        if (count <= 0) {
            throw std::invalid_argument("Thread pool size must be positive");
        }
    }

    thread_pool::~thread_pool()
    {
        // Safe to call multiple times - destructor ensures cleanup
        try {
            stop();
            wait();
        } catch (...) {
            // Don't let exceptions escape destructor
            // Threads should be joinable, but just in case...
        }
    }

    void thread_pool::worker_thread()
    {
        while (true) {
            work_element work;
            
            {
                std::unique_lock<std::mutex> lock(work_mutex);
                
                // Wait for work or shutdown signal
                work_condition.wait(lock, [this] {
                    return should_shutdown.load() || !work_items.empty();
                });
                
                // Check for shutdown
                if (should_shutdown.load() && work_items.empty()) {
                    return;
                }
                
                // Get work item
                if (!work_items.empty()) {
                    work = std::move(work_items.front());
                    work_items.pop();
                }
            }
            
            // Execute work item with exception safety
            if (work) {
                try {
                    work();
                } catch (const std::exception& e) {
                    LOG_ERROR("Exception in thread pool worker: " << e.what());
                } catch (...) {
                    LOG_ERROR("Unknown exception in thread pool worker");
                }
            }
        }
    }

    void thread_pool::start()
    {
        if (running.load()) {
            LOG_WARN("Thread pool already running");
            return;
        }
        
        should_shutdown.store(false);
        running.store(true);
        
        try {
            threads.reserve(thread_count);
            for (int i = 0; i < thread_count; ++i) {
                threads.emplace_back(&thread_pool::worker_thread, this);
            }
        } catch (...) {
            // Cleanup on failure
            running.store(false);
            should_shutdown.store(true);
            work_condition.notify_all();
            
            // Join any threads that were created
            for (auto& thread : threads) {
                if (thread.joinable()) {
                    thread.join();
                }
            }
            threads.clear();
            throw;
        }
    }

    void thread_pool::stop()
    {
        // Idempotent: safe to call multiple times
        // Only signal shutdown if we haven't already
        bool was_shutdown = should_shutdown.exchange(true);
        if (was_shutdown) {
            return;  // Already stopped
        }
        
        // Signal all workers to wake up and check shutdown flag
        work_condition.notify_all();
    }

    void thread_pool::wait()
    {
        // Idempotent: safe to call multiple times
        if (!running.load()) {
            return;  // Already stopped and waited
        }
        
        // Wait for all threads to complete
        for (auto& thread : threads) {
            if (thread.joinable()) {
                thread.join();
            }
        }
        
        threads.clear();
        running.store(false);
        
        // Clear any remaining work items
        {
            std::lock_guard<std::mutex> lock(work_mutex);
            std::queue<work_element> empty_queue;
            work_items.swap(empty_queue);
        }
    }

    bool thread_pool::queue_work_item(work_element work)
    {
        if (!running.load()) {
            return false;  // Thread pool not running
        }
        
        if (should_shutdown.load()) {
            return false;  // Thread pool stopping
        }
        
        {
            std::lock_guard<std::mutex> lock(work_mutex);
            // Double-check shutdown status under lock to avoid race
            if (should_shutdown.load()) {
                return false;  // Thread pool stopping
            }
            work_items.emplace(std::move(work));
        }
        work_condition.notify_one();
        return true;  // Successfully queued
    }

    bool thread_pool::begin_queue_work_item()
    {
        if (!running.load() || should_shutdown.load()) {
            return false;  // Thread pool not running or stopping
        }
        
        work_mutex.lock();
        
        // Double-check after acquiring lock
        if (should_shutdown.load()) {
            work_mutex.unlock();
            return false;  // Thread pool stopping
        }
        
        return true;  // Successfully started batch mode
    }

    bool thread_pool::queue_work_item_batch(work_element work)
    {
        // Assumes caller has locked work_mutex via begin_queue_work_item()
        // Check shutdown status (caller should have checked, but be safe)
        if (should_shutdown.load()) {
            return false;  // Thread pool stopping
        }
        
        work_items.emplace(std::move(work));
        return true;  // Successfully queued
    }
    
    void thread_pool::end_queue_work_item()
    {
        work_mutex.unlock();
        work_condition.notify_all();  // Notify all since we may have queued multiple items
    }    size_t thread_pool::get_queue_size() const
    {
        std::lock_guard<std::mutex> lock(work_mutex);
        return work_items.size();
    }

}
