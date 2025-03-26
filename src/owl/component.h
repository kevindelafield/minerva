#pragma once

#include <set>
#include <vector>
#include <mutex>
#include <functional>
#include <condition_variable>
#include <util/thread_pool.h>
#include <util/scheduler.h>

namespace minerva
{

    class component_visor;

    class component
    {
    public:
        component() = default;
        
        virtual ~component() = default;
        
        component_visor* visor;

        template<class T>
            T* get_component(const std::string & name) const
        {
            auto cmp = get_component_internal(name);
            return dynamic_cast<T*>(cmp);
        }
        
        bool should_shutdown() const;
        
        virtual std::string name() = 0;
        
        virtual void initialize();
        
        virtual void start();
        
        virtual void stop();
        
        virtual void wait();
        
        virtual void release();
        
        virtual void hup();
        
        bool save_settings();

    protected:
        
        std::mutex lock;
        std::condition_variable cond;
        
        minerva::thread_pool * add_thread_pool(int count);

        void add_thread(const std::function<void()> &);
        
        minerva::scheduler::job_handle schedule_job(const minerva::scheduler::job_element & job, int ms);
        
        bool cancel_job(const minerva::scheduler::job_handle & handle);

    private:
        friend class component_visor;
        std::mutex m_shutdown_mutex;
        std::condition_variable m_shutdown_cond;
        component * get_component_internal(const std::string & name) const;
            
    };
}
