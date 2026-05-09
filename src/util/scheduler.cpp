#include <vector>
#include <utility>
#include "scheduler.h"
#include "log.h"

namespace minerva
{
    namespace
    {
        // Cap on how long the timer thread sleeps when there is no work
        // pending. Keeps the loop responsive to external state changes
        // even though scheduling and shutdown both signal the cv.
        constexpr std::chrono::seconds IDLE_POLL_CAP{60};
    }

    scheduler::scheduler(int threads) : tp(threads)
    {
    }

    scheduler::~scheduler()
    {
        if (state.load() != STOPPED)
        {
            stop();
            wait();
        }
    }

    scheduler::job_handle scheduler::schedule_job(job_element job,
                                                  std::chrono::milliseconds duration)
    {
        const auto when  = clock::now() + duration;
        auto       entry = std::make_shared<job_entry>(std::move(job));

        {
            std::lock_guard<std::mutex> lk(mtx);
            jobs.emplace(when, entry);
        }
        cond.notify_one();

        return job_handle(when, std::weak_ptr<job_entry>(entry));
    }

    bool scheduler::cancel_job(const job_handle& handle)
    {
        // Locking the weak_ptr also serves as the "is this a real handle?"
        // check: a default-constructed handle has an empty weak_ptr and
        // returns nullptr here.
        auto entry = handle.m_entry.lock();
        if (!entry)
        {
            return false;
        }

        std::lock_guard<std::mutex> lk(mtx);
        auto range = jobs.equal_range(handle.m_when);
        for (auto it = range.first; it != range.second; ++it)
        {
            if (it->second == entry)
            {
                jobs.erase(it);
                return true;
            }
        }
        return false;
    }

    void scheduler::run_jobs(std::vector<job_element>& batch)
    {
        batch.clear();
        {
            std::unique_lock<std::mutex> lk(mtx);

            // Wait until a job is due, the cv is signaled, or the idle cap
            // expires. The predicate also handles spurious wakeups.
            const auto now = clock::now();
            time_point next = now + IDLE_POLL_CAP;
            if (!jobs.empty())
            {
                next = jobs.begin()->first;
            }

            cond.wait_until(lk, next, [&] {
                return should_shutdown.load() ||
                       (!jobs.empty() && jobs.begin()->first <= clock::now());
            });

            if (should_shutdown.load())
            {
                return;
            }

            // Drain all expired jobs in one pass, moving the function objects
            // out so we don't pay an extra std::function copy.
            const auto cutoff = clock::now();
            for (auto it = jobs.begin();
                 it != jobs.end() && it->first <= cutoff; )
            {
                batch.emplace_back(std::move(it->second->job));
                it = jobs.erase(it);
            }
        }

        if (batch.empty())
        {
            return;
        }

        // Dispatch outside the scheduler lock so worker callbacks that
        // reschedule won't deadlock.
        if (!tp.begin_queue_work_item())
        {
            // Pool is stopping; drop the batch (jobs are best-effort).
            return;
        }
        for (auto& j : batch)
        {
            tp.queue_work_item_batch([fn = std::move(j)]() {
                try
                {
                    fn();
                }
                catch (const std::exception& e)
                {
                    LOG_ERROR("Scheduled job threw exception: " << e.what());
                }
                catch (...)
                {
                    LOG_ERROR("Scheduled job threw unknown exception");
                }
            });
        }
        tp.end_queue_work_item();
    }

    void scheduler::run()
    {
        std::vector<job_element> batch;
        while (!should_shutdown.load())
        {
            run_jobs(batch);
        }
    }

    void scheduler::start()
    {
        state_t expected = STOPPED;
        if (!state.compare_exchange_strong(expected, RUNNING))
        {
            LOG_WARN("scheduler::start: already started (state=" << expected << ")");
            return;
        }
        should_shutdown.store(false);
        // Start the pool *before* the timer thread, so any immediate dispatch
        // lands in a fully initialized pool.
        tp.start();
        t = std::make_unique<std::thread>(&scheduler::run, this);
    }

    void scheduler::stop()
    {
        // Mark state STOPPING and request shutdown. Notify under the lock so
        // the timer thread sees the flag, but call tp.stop() *outside* the
        // lock to avoid a fixed lock order between scheduler::mtx and the
        // pool's internal mutex.
        state_t expected = RUNNING;
        if (!state.compare_exchange_strong(expected, STOPPING))
        {
            // Already stopped/stopping; idempotent.
            return;
        }
        {
            std::lock_guard<std::mutex> lk(mtx);
            should_shutdown.store(true);
        }
        cond.notify_one();
        tp.stop();
    }

    void scheduler::wait()
    {
        // Idempotent: nothing to wait on if we're already stopped.
        if (state.load() == STOPPED)
        {
            return;
        }
        if (t && t->joinable())
        {
            t->join();
        }
        t.reset();
        tp.wait();
        state.store(STOPPED);
    }
}
