#pragma once

#include <chrono>
#include <set>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <memory>
#include <functional>
#include "thread_pool.h"

namespace owl
{

    class scheduler
    {
    public:
        typedef std::function<void()> job_element;

    private:

        class job_entry
        {
        public:
            job_element job;

            inline job_entry(job_element job) : job(job)
            {
            }

        };

        std::thread* t;
        std::mutex lock;
        std::condition_variable cond;
        std::set<std::tuple<std::chrono::steady_clock::time_point,
            std::shared_ptr<job_entry>>> jobs;
        thread_pool tp;
        volatile bool should_shutdown;
        bool running;

        void wait_for_signal();
        void run_jobs();
        void run();

    public:
        scheduler(int threads = 5);
        virtual ~scheduler();

        class job_handle
        {
        friend scheduler;
        public:
            job_handle()
            {
                
            }

            job_handle(const job_handle & job) = default;

        private:

            job_handle(std::tuple<std::chrono::steady_clock::time_point,
                       std::shared_ptr<job_entry>> job) : m_entry(job)
            {
                
            }

            const std::tuple<std::chrono::steady_clock::time_point,
                std::shared_ptr<job_entry>> & entry() const
                {
                    return m_entry;
                }

            std::tuple<std::chrono::steady_clock::time_point,
                std::shared_ptr<job_entry>> m_entry;
        };

        job_handle schedule_job(job_element job, std::chrono::milliseconds duration);

        bool cancel_job(const job_handle & handle);

        void start();

        void stop();

        void wait();
    };
}
