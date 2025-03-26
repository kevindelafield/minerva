#include <algorithm>
#include <functional>
#include <cassert>
#include <signal.h>
#include <util/log.h>
#include <util/safe_ofstream.h>
#include <util/file_utils.h>
#include "component_visor.h"

namespace minerva
{

    component_visor::component_visor() : running(false), 
                                         m_should_shutdown(false), 
                                         m_stopped(false)
    {
    }

    component_visor::~component_visor()
    {
    }

    void component_visor::add(component * cmp)
    {
        assert(!running);
        assert(cmp);
        LOG_DEBUG("adding component: " << cmp->name());
        components.emplace(cmp->name(),
                           std::move(std::unique_ptr<component>(cmp)));
        cmp->visor = this;
    }

    void component_visor::add_thread(const std::function<void()> & fp)
    {
        std::unique_lock<std::mutex> lk(lock);
        
        assert(!running);
        assert(!m_should_shutdown);
        
        _thread_functions.push_back(fp);
    }

    void component_visor::clear()
    {
        assert(!running);
        components.clear();
    }

    void component_visor::initialize()
    {
        assert(!running);

        std::for_each(components.begin(), components.end(),
                      [](auto & it) {
                          it.second->initialize();
                      });
    }

    void component_visor::start()
    {
        assert(!running);

        // start components
        std::for_each(components.begin(), components.end(),
                      [](auto & it) {
                          it.second->start();
                      });

        // start threads
        std::for_each(_thread_functions.begin(), _thread_functions.end(),
                  [this] (auto & it) {
                      auto t = new std::thread(it);
                      assert(t);
                      this->threads.insert(t);
                  });

        // start thread pools
        std::for_each(thread_pools.begin(), thread_pools.end(),
                      [] (minerva::thread_pool * tp) {
                          tp->start();
                      });

        sched.start();

        running = true;
    }

    void component_visor::stop()
    {
        std::unique_lock<std::mutex> lk(lock);

        if (m_stopped)
        {
            return;
        }

        m_should_shutdown = true;

        sched.stop();

        std::for_each(components.begin(), components.end(),
                      [](auto & it) {
                          it.second->stop();
                      });
        std::for_each(thread_pools.begin(), thread_pools.end(),
                      [] (minerva::thread_pool * tp) {
                          tp->stop();
                      });

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

        sched.wait();

        std::for_each(components.begin(), components.end(),
                      [](auto & it) {
                          it.second->wait();
                      });

        std::for_each(threads.begin(), threads.end(), [](std::thread* t) {
                t->join();
            });
        std::for_each(thread_pools.begin(), thread_pools.end(),
                      [] (minerva::thread_pool * tp) {
                          tp->wait();
                      });
        running = false;
    }

    void component_visor::release()
    {
        assert(!running);

        sched.release();

        std::for_each(components.begin(), components.end(),
                      [](auto & it) {
                          it.second->release();
                      });

        std::for_each(thread_pools.begin(), thread_pools.end(), [](minerva::thread_pool * tp) {
            tp->release();
            delete tp;
        });
        thread_pools.clear();

        std::for_each(threads.begin(), threads.end(), [](std::thread* t) {
            delete t;
        });
        threads.clear();
    }

    void component_visor::hup()
    {
        if (!running)
        {
            return;
        }

        LOG_INFO("HUP");
        std::for_each(components.begin(), components.end(),
                      [](auto & it) {
                          it.second->hup();
                      });
    }

}
