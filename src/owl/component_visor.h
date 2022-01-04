#pragma once

#include <mutex>
#include <condition_variable>
#include <map>
#include <functional>
#include <thread>
#include <set>
#include <cassert>
#include <jsoncpp/json/json.h>
#include <util/thread_pool.h>
#include <util/scheduler.h>
#include "component.h"

namespace owl
{

    class component_visor
    {
    private:

        constexpr static char SETTINGS_FILE_KEY[] = "settings_file_name";

        std::vector<std::function<void()>> _thread_functions;
        std::map<std::string, std::unique_ptr<component>> components;
        std::set<std::thread*> threads;
        std::set<util::thread_pool *> thread_pools;
        util::scheduler sched;
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

        void add(component * cmp);

        util::scheduler::job_handle schedule_job(util::scheduler::job_element job, int milliseconds)
        {
            return sched.schedule_job(job, std::chrono::milliseconds(milliseconds));
        }

        bool cancel_job(const util::scheduler::job_handle & handle)
        {
            return sched.cancel_job(handle);
        }

        void add_thread(std::function<void()> routine);

        util::thread_pool * add_thread_pool(int count)
        {
            std::unique_lock<std::mutex> lk(lock);
            assert(!running);
            auto tp = new util::thread_pool(count);
            assert(tp);
            thread_pools.insert(tp);
            return tp;
        }

        template<class T>
            T * get_component(const std::string & name) const
        {
            const auto & entry = components.find(name);
            if (entry != components.end())
            {
                return dynamic_cast<T*>(entry->second.get());
            }
            return nullptr;
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
