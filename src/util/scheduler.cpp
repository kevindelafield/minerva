#include <algorithm>
#include <vector>
#include <cassert>
#include "scheduler.h"
#include "log.h"

namespace minerva
{

    scheduler::scheduler(int threads) : should_shutdown(false), running(false), t(NULL), tp(threads)
    {
    }

    scheduler::~scheduler()
    {
        assert(!running);
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
        lock.lock();
        jobs.insert(tuple);
        cond.notify_one();
        lock.unlock();

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

        // get next run
        auto it = std::min_element(jobs.begin(), jobs.end(),
                                   [] (auto j1, auto j2)
                                   {
                                       return std::get<0>(j1) < std::get<0>(j2);
                                   });

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
        lock.lock();

        auto first_to_remove(jobs.end());
        auto last_to_remove(jobs.begin());

        auto now = std::chrono::steady_clock::now();

        // find all expired jobs
        // scan the list until times not expired
        for (auto it = jobs.begin(); it != jobs.end(); it++)
        {
            if (std::get<0>(*it) <= now)
            {
                last_to_remove++;
                if (first_to_remove == jobs.end())
                {
                    first_to_remove = it;
                }
            }
            else
            {
                break;
            }
        }

        // copy jobs to run and remove them from the jobs
        std::vector<job_element> to_run;
        if (first_to_remove != jobs.end())
        {
            for (auto it = first_to_remove; it != last_to_remove; it++)
            {
                to_run.push_back(std::get<1>(*it)->job);
            }
            jobs.erase(first_to_remove, last_to_remove);
        }

        lock.unlock();

        // run jobs
        tp.begin_queue_work_item();
        std::for_each(to_run.begin(), to_run.end(), [this] (auto it) {
                this->tp.queue_work_item_batch([it]()
                                               {
                                                   it();
                                               });
                    });
        tp.end_queue_work_item();
    }

    void scheduler::run()
    {
        while (!should_shutdown)
        {
            wait_for_signal();
            run_jobs();
        }
    }

    void scheduler::start()
    {
        assert(!running);
        t = new std::thread(&scheduler::run, this);    
        assert(t);
        tp.start();
        running = true;
    }

    void scheduler::stop()
    {
        lock.lock();
        should_shutdown = true;
        cond.notify_one();
        lock.unlock();
        tp.stop();
    }

    void scheduler::wait()
    {
        assert(running);
        t->join();
        tp.wait();
        running = false;
    }

    void scheduler::release()
    {
        delete t;
        t = NULL;
        tp.release();
    }
}
