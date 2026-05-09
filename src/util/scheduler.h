#pragma once

#include <chrono>
#include <map>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <memory>
#include <functional>
#include <atomic>
#include "thread_pool.h"

namespace minerva
{

    /**
     * Single-threaded timer that dispatches expired jobs into a thread_pool.
     *
     * Cancellation semantics:
     *   cancel_job() removes a job from the pending queue if it has not yet
     *   been dispatched. It returns true if the job was found and removed.
     *   It does NOT wait for an in-flight job to finish, and it cannot stop
     *   a job that has already been handed to the thread pool. Callers that
     *   need stronger guarantees must coordinate with the job body itself
     *   (e.g. an atomic "alive" flag or a shared_ptr the job captures).
     */
    class scheduler
    {
    public:
        typedef std::function<void()> job_element;

    private:
        struct job_entry
        {
            job_element job;
            explicit job_entry(job_element j) : job(std::move(j)) {}
        };

        using clock      = std::chrono::steady_clock;
        using time_point = clock::time_point;
        using job_map    = std::multimap<time_point, std::shared_ptr<job_entry>>;

        enum state_t { STOPPED, RUNNING, STOPPING };

        std::unique_ptr<std::thread> t;
        std::mutex                   mtx;
        std::condition_variable      cond;
        job_map                      jobs;
        thread_pool                  tp;
        std::atomic<bool>            should_shutdown{false};
        std::atomic<state_t>         state{STOPPED};

        void run();
        void run_jobs(std::vector<job_element>& batch);

    public:
        explicit scheduler(int threads = 5);
        ~scheduler();

        scheduler(const scheduler&)            = delete;
        scheduler& operator=(const scheduler&) = delete;
        scheduler(scheduler&&)                 = delete;
        scheduler& operator=(scheduler&&)      = delete;

        class job_handle
        {
            friend class scheduler;
        public:
            job_handle() = default;
            job_handle(const job_handle&)            = default;
            job_handle(job_handle&&)                 = default;
            job_handle& operator=(const job_handle&) = default;
            job_handle& operator=(job_handle&&)      = default;

        private:
            job_handle(time_point when, std::weak_ptr<job_entry> entry)
                : m_when(when), m_entry(std::move(entry)) {}

            time_point               m_when{};
            std::weak_ptr<job_entry> m_entry;
        };

        job_handle schedule_job(job_element job, std::chrono::milliseconds duration);

        bool cancel_job(const job_handle& handle);

        void start();
        void stop();
        void wait();
    };
}
