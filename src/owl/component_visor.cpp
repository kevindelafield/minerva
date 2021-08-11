#include <algorithm>
#include <functional>
#include <cassert>
#include <signal.h>
#include "log.h"
#include "component_visor.h"
#include "safe_ofstream.h"
#include "json_utils.h"
#include "file_utils.h"

namespace owl
{

    component_visor::component_visor() : running(false), 
                                         m_should_shutdown(false), 
                                         m_stopped(false)
    {
    }

    component_visor::~component_visor()
    {
    }

    void component_visor::add(std::shared_ptr<component> cmp)
    {
        assert(!running);
        assert(cmp);
        LOG_DEBUG("adding component: " << cmp->name());
        components[cmp->name()] = cmp;
        cmp->visor = this;
    }

    void component_visor::add_thread(std::function<void()> fp)
    {
        std::unique_lock<std::mutex> lk(lock);
        
        assert(!running);
        assert(!m_should_shutdown);
        
        _thread_functions.push_back(fp);
    }

    Json::Value component_visor::get_stats()
    {
        Json::Value stats;

        lock.lock();    
        auto tmp = components;
        lock.unlock();
    
        for (auto k : tmp)
        {
            stats[k.first] = k.second->get_stats();
        }

        return stats;
    }

    void component_visor::clear()
    {
        assert(!running);
        lock.lock();
        components.clear();
        lock.unlock();
    }

    void component_visor::initialize()
    {
        assert(!running);

        std::for_each(components.begin(), components.end(),
                      [](auto it) {
                          it.second->initialize();
                      });
    }

    void component_visor::start()
    {
        assert(!running);

        lock.lock();    
        auto tmp = components;
        lock.unlock();
    
        // start components
        std::for_each(tmp.begin(), tmp.end(),
                      [](auto it) {
                          it.second->start();
                      });

        // start threads
        std::for_each(_thread_functions.begin(), _thread_functions.end(),
                  [this] (auto it) {
                      auto t = new std::thread(it);
                      assert(t);
                      this->threads.insert(t);
                  });

        // start thread pools
        std::for_each(thread_pools.begin(), thread_pools.end(),
                      [] (std::shared_ptr<thread_pool> tp) {
                          tp->start();
                      });


        sched.start();

        running = true;
    }

    void component_visor::stop()
    {
        m_should_shutdown = true;

        lock.lock();    
        auto tmp = components;
        lock.unlock();
    
        sched.stop();

        std::for_each(threads.begin(), threads.end(),
                      [] (std::thread *thread) {
                          pthread_kill(thread->native_handle(), SIGUSR2);
                      });

        std::for_each(threads.begin(), threads.end(),
                      [] (std::thread *thread) {
                          pthread_kill(thread->native_handle(), SIGUSR2);
                      });

        std::for_each(tmp.begin(), tmp.end(),
                      [](auto it) {
                          it.second->stop();
                      });
        std::for_each(thread_pools.begin(), thread_pools.end(),
                      [] (std::shared_ptr<thread_pool> tp) {
                          tp->stop();
                      });

        std::unique_lock<std::mutex> lk(lock);
        m_stopped = true;
        cond.notify_all();
    }

    void component_visor::wait()
    {
        assert(running);

        {
            std::unique_lock<std::mutex> lk(lock);
            while (!m_stopped)
            {
                cond.wait(lk);
            }
        }

        lock.lock();    
        auto tmp = components;
        lock.unlock();
    
        sched.wait();

        std::for_each(tmp.begin(), tmp.end(),
                      [](auto it) {
                          it.second->wait();
                      });

        std::for_each(threads.begin(), threads.end(), [](std::thread* t) {
                t->join();
            });
        std::for_each(threads.begin(), threads.end(), [](std::thread* t) {
                delete t;
            });
        threads.clear();
        std::for_each(thread_pools.begin(), thread_pools.end(),
                      [] (std::shared_ptr<thread_pool> tp) {
                          tp->wait();
                      });
        thread_pools.clear();

        running = false;
    }

    void component_visor::release()
    {
        assert(!running);

        std::for_each(components.begin(), components.end(),
                      [](auto it) {
                          it.second->release();
                      });
    }

    void component_visor::hup()
    {
        if (!running)
        {
            return;
        }

        LOG_INFO("HUP");
        std::for_each(components.begin(), components.end(),
                      [](auto it) {
                          it.second->hup();
                      });
    }

}
