#include <sys/epoll.h>
#include <unistd.h>
#include <cstring>
#include <cassert>
#include <functional>
#include <chrono>
#include "component.h"
#include "log.h"
#include "component_visor.h"

namespace owl
{
    std::shared_ptr<component> component::get_component_internal(const std::string & name) const
    {
        return visor->get_component<component>(name);
    }
    
    Json::Value component::get_stats()
    {
        Json::Value v;
        return v;
    }
    
    std::shared_ptr<thread_pool> component::add_thread_pool(int count)
    {
        return visor->add_thread_pool(count);
    }
    
    
    bool component::should_shutdown() const
    {
        return visor->should_shutdown();
    }
    
    void component::add_thread(std::function<void()> routine)
    {
        visor->add_thread(routine);
    }
    
    void component::initialize()
    {
    }
    
    void component::start()
    {
    }
    
    void component::wait()
    {
    }
    
    void component::release()
    {
    }
    
    void component::hup()
    {
    }

    void component::stop()
    {
        m_shutdown_mutex.lock();
        m_shutdown_cond.notify_all();
        m_shutdown_mutex.unlock();

        lock.lock();
        cond.notify_all();
        lock.unlock();
    }
    
    scheduler::job_handle component::schedule_job(scheduler::job_element job, int ms)
    {
        return visor->schedule_job(job, ms);
    }

    bool component::cancel_job(const scheduler::job_handle & handle)
    {
        return visor->cancel_job(handle);
    }
}
