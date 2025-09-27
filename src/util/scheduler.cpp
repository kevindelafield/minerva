#include <algorithm>
#include <vector>
#include <cassert>
#include <memory>
#include "scheduler.h"
#include "log.h"

namespace minerva
{

    scheduler::scheduler(int threads) : should_shutdown(false), running(false), t(nullptr), tp(threads)
    {
    }

    scheduler::~scheduler()
    {
        if (running)
        {
            stop();
            wait();
        }
        // unique_ptr automatically cleans up the thread
    }

    scheduler::job_handle scheduler::schedule_job(job_element job,
                                 std::chrono::milliseconds duration)
    {
        auto when = std::chrono::steady_clock::now() + duration;

        // have to put the job_element in a wrapper to satisfy stl
        auto entry = std::shared_ptr<job_entry>(new job_entry(job));
        assert(entry);

        auto tuple = std::make_tuple(when, entry);

        // add the job and notify the thread
        {
            std::lock_guard<std::mutex> lk(lock);
            jobs.insert(tuple);
            cond.notify_one();
        }

        return job_handle(tuple);
    }

    bool scheduler::cancel_job(const job_handle & handle)
    {
        std::unique_lock<std::mutex> lk(lock);
        
        // the epoch
        auto epoch = std::chrono::time_point<std::chrono::steady_clock>();

        // job was never scheduled - assume cancelled
        if (std::get<0>(handle.entry()) == epoch)
        {
            return true;
        }

        auto search = jobs.find(handle.entry());
        if (search != jobs.end())
        {
            jobs.erase(search);
            return true;
        }

        return false;
    }

    void scheduler::wait_for_signal()
    {
        std::unique_lock<std::mutex> lk(lock);

        // get next run - set is already sorted, so just use begin()
        auto it = jobs.begin();

        auto now = std::chrono::steady_clock::now();

        auto next_run = now + std::chrono::seconds(60);
        if (it != jobs.end()) {
            next_run = std::get<0>(*it);
        }

        // wait for a signal or until the next job is scheduled
        if (next_run > now)
        {
            cond.wait_for(lk, next_run - now + std::chrono::microseconds(1));
        }
    }

    void scheduler::run_jobs()
    {
        std::vector<job_element> to_run;
        
        // Critical section: find and extract expired jobs
        {
            std::lock_guard<std::mutex> lk(lock);
            
            auto now = std::chrono::steady_clock::now();

            // Find all expired jobs (jobs are sorted earliest→latest)
            auto expired_end = jobs.begin();
            for (auto it = jobs.begin(); it != jobs.end(); ++it)
            {
                if (std::get<0>(*it) <= now)
                {
                    expired_end = std::next(it);  // Point to one past the last expired job
                }
                else
                {
                    break;  // Since sorted, no more expired jobs
                }
            }

            // Copy expired jobs to run and remove them from the set
            for (auto it = jobs.begin(); it != expired_end; ++it)
            {
                to_run.push_back(std::get<1>(*it)->job);
            }
            
            if (expired_end != jobs.begin())
            {
                jobs.erase(jobs.begin(), expired_end);
            }
        }

        // run jobs with exception safety
        tp.begin_queue_work_item();
        std::for_each(to_run.begin(), to_run.end(), [this] (auto it) {
                this->tp.queue_work_item_batch([it]()
                                               {
                                                   try
                                                   {
                                                       it();
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
                    });
        tp.end_queue_work_item();
    }

    void scheduler::run()
    {
        while (!should_shutdown.load())
        {
            wait_for_signal();
            run_jobs();
        }
    }

    void scheduler::start()
    {
        assert(!running);
        t = std::make_unique<std::thread>(&scheduler::run, this);
        assert(t);
        tp.start();
        running = true;
    }

    void scheduler::stop()
    {
        std::unique_lock<std::mutex> lk(lock);
        should_shutdown.store(true);
        cond.notify_one();
        tp.stop();  // Now called within the lock for thread safety
    }

    void scheduler::wait()
    {
        assert(running);
        t->join();
        tp.wait();  // Wait for thread pool threads to exit
        running = false;
    }


}
