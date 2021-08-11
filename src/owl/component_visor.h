#pragma once

#include <memory>
#include <mutex>
#include <condition_variable>
#include <map>
#include <functional>
#include <thread>
#include <set>
#include <cassert>
#include <jsoncpp/json/json.h>
#include "component.h"
#include "thread_pool.h"
#include "scheduler.h"

namespace owl
{

    class component_visor
    {
    private:

        constexpr static char SETTINGS_FILE_KEY[] = "settings_file_name";

        std::vector<std::function<void()>> _thread_functions;
        std::map<std::string, std::shared_ptr<component>> components;
        std::set<std::thread*> threads;
        std::set<std::shared_ptr<thread_pool>> thread_pools;
        scheduler sched;
        bool running;
        std::mutex lock;
        std::condition_variable cond;
        volatile bool m_should_shutdown;
        volatile bool m_stopped;

    public:
        component_visor();
        virtual ~component_visor();

        bool save_settings();

        bool should_shutdown() const
        {
            return m_should_shutdown;
        }

        void add(std::shared_ptr<component> cmp);

        scheduler::job_handle schedule_job(scheduler::job_element job, int milliseconds)
        {
            return sched.schedule_job(job, std::chrono::milliseconds(milliseconds));
        }

        bool cancel_job(const scheduler::job_handle & handle)
        {
            return sched.cancel_job(handle);
        }

        void add_thread(std::function<void()> routine);

        std::shared_ptr<thread_pool> add_thread_pool(int count)
        {
            std::unique_lock<std::mutex> lk(lock);
            assert(!running);
            auto tp = std::make_shared<thread_pool>(count);
            assert(tp);
            thread_pools.insert(tp);
            return tp;
        }

        template<class T>
            std::shared_ptr<T> get_component(const std::string & name) const
        {
            std::shared_ptr<T> cmp;

            const auto & entry = components.find(name);
            if (entry != components.end())
            {
                cmp = std::dynamic_pointer_cast<T>(entry->second);
            }
            return cmp;
        }

        void clear();

        void initialize();

        void start();

        void stop();

        void wait();

        void release();

        void hup();

        Json::Value get_stats();

    };
}
