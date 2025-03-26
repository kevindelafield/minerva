#include <sys/epoll.h>
#include <unistd.h>
#include <cstring>
#include <cassert>
#include <functional>
#include <chrono>
#include <util/log.h>
#include <util/thread_pool.h>
#include "component.h"
#include "component_visor.h"

namespace minerva
{
    component * component::get_component_internal(const std::string & name) const
    {
        return visor->get_component<component>(name);
    }
    
    minerva::thread_pool * component::add_thread_pool(int count)
    {
        return visor->add_thread_pool(count);
    }
    
    
    bool component::should_shutdown() const
    {
        return visor->should_shutdown();
    }
    
    void component::add_thread(const std::function<void()> & routine)
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
    
    minerva::scheduler::job_handle component::schedule_job(const minerva::scheduler::job_element & job, int ms)
    {
        return visor->schedule_job(job, ms);
    }

    bool component::cancel_job(const minerva::scheduler::job_handle & handle)
    {
        return visor->cancel_job(handle);
    }
}
