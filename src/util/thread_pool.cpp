#include <stdexcept>
#include "thread_pool.h"
#include "log.h"

namespace minerva
{
    thread_pool::thread_pool(int count)
        : thread_count(count)
    {
        if (count <= 0)
        {
            throw std::invalid_argument("Thread pool size must be positive");
        }
    }

    thread_pool::~thread_pool()
    {
        stop();
        wait();
    }

    void thread_pool::worker_thread()
    {
        while (true)
        {
            work_element work;
            {
                std::unique_lock<std::mutex> lock(work_mutex);
                work_condition.wait(lock, [this] {
                    return should_shutdown.load() || !work_items.empty();
                });

                if (work_items.empty())
                {
                    // wakeup with empty queue => shutdown
                    return;
                }

                work = std::move(work_items.front());
                work_items.pop();
            }

            // Run outside the lock. Exceptions from user callbacks are
            // logged and swallowed so a misbehaving job doesn't tear down
            // the worker. submit()-style callers receive exceptions through
            // their future instead.
            if (work)
            {
                try
                {
                    work();
                }
                catch (const std::exception& e)
                {
                    LOG_ERROR("Exception in thread pool worker: " << e.what());
                }
                catch (...)
                {
                    LOG_ERROR("Unknown exception in thread pool worker");
                }
            }
        }
    }

    void thread_pool::start()
    {
        state_t expected = STOPPED;
        if (!state.compare_exchange_strong(expected, RUNNING))
        {
            LOG_ERROR("thread_pool::start: already started or stopping (state="
                      << expected << ")");
            return;
        }

        should_shutdown.store(false);
        threads.reserve(static_cast<std::size_t>(thread_count));
        for (int i = 0; i < thread_count; ++i)
        {
            threads.emplace_back(&thread_pool::worker_thread, this);
        }
    }

    void thread_pool::stop()
    {
        // Idempotent: only the first caller transitions RUNNING -> STOPPING.
        state_t expected = RUNNING;
        if (!state.compare_exchange_strong(expected, STOPPING))
        {
            // If we're already STOPPING or STOPPED, just make sure the flag
            // is set so callers transitioning from a partially-initialized
            // state still see it.
            should_shutdown.store(true);
            work_condition.notify_all();
            return;
        }

        should_shutdown.store(true);
        work_condition.notify_all();
    }

    void thread_pool::wait()
    {
        if (state.load() == STOPPED)
        {
            return;
        }

        for (auto& t : threads)
        {
            if (t.joinable()) t.join();
        }
        threads.clear();

        // Workers exit only when both should_shutdown is set and the queue
        // is empty, so there is no work left to discard here.
        state.store(STOPPED);
    }

    bool thread_pool::queue_work_item(work_element work)
    {
        std::lock_guard<std::mutex> lock(work_mutex);
        if (state.load() != RUNNING || should_shutdown.load())
        {
            return false;
        }
        work_items.emplace(std::move(work));
        // notify_one is sufficient; one item, one worker.
        work_condition.notify_one();
        return true;
    }

    bool thread_pool::begin_queue_work_item()
    {
        // Always take the lock so end_queue_work_item can always unlock.
        // The bool return reports whether the pool is in a state that will
        // accept new items; callers may ignore it.
        work_mutex.lock();
        return state.load() == RUNNING && !should_shutdown.load();
    }

    bool thread_pool::queue_work_item_batch(work_element work)
    {
        // Caller holds work_mutex via begin_queue_work_item.
        if (state.load() != RUNNING || should_shutdown.load())
        {
            return false;
        }
        work_items.emplace(std::move(work));
        return true;
    }

    void thread_pool::end_queue_work_item()
    {
        work_mutex.unlock();
        work_condition.notify_all();
    }

    size_t thread_pool::get_queue_size() const
    {
        std::lock_guard<std::mutex> lock(work_mutex);
        return work_items.size();
    }
}
