#pragma once

#include <set>
#include <vector>
#include <mutex>
#include <memory>
#include <functional>
#include <condition_variable>
#include <jsoncpp/json/json.h>
#include "thread_pool.h"
#include "scheduler.h"

namespace owl
{

    class component_visor;

    class component
    {
    public:
        component() = default;
        
        virtual ~component() = default;
        
        component_visor* visor;

        template<class T>
            std::shared_ptr<T> get_component(const std::string & name) const
        {
            auto cmp = get_component_internal(name);
            return std::dynamic_pointer_cast<T>(cmp);
        }
        
        bool should_shutdown() const;
        
        virtual std::string name() = 0;
        
        virtual void initialize();
        
        virtual void start();
        
        virtual void stop();
        
        virtual void wait();
        
        virtual void release();
        
        virtual void hup();
        
        virtual Json::Value get_stats();

        bool get_setting(const std::string & name, Json::Value & value);

        void set_setting(const std::string & name, const Json::Value & value);

        bool save_settings();

    protected:
        
        std::mutex lock;
        std::condition_variable cond;
        
        std::shared_ptr<thread_pool> add_thread_pool(int count);

        void add_thread(std::function<void()>);
        
        scheduler::job_handle schedule_job(scheduler::job_element job, int ms);
        
        bool cancel_job(const scheduler::job_handle & handle);

    private:
        friend class component_visor;
        std::mutex m_shutdown_mutex;
        std::condition_variable m_shutdown_cond;
        std::shared_ptr<component> get_component_internal(const std::string & name) const;
            
    };
}
