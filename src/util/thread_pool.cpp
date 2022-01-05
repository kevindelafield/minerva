#include <algorithm>
#include <cassert>
#include <signal.h>
#include "thread_pool.h"

namespace util
{

    thread_pool::thread_pool(int count) :
        thread_count(count), running(false), should_shutdown(false)
    {
        assert(thread_count > 0);
    }

    thread_pool::~thread_pool()
    {
        assert(!running);
    }

    void thread_pool::run()
    {
        while (!should_shutdown)
        {
            work_element work_item = NULL;
            {
                std::unique_lock<std::mutex> lk(lock);
                if (should_shutdown)
                {
                    return;
                }
                while (work_items.size() == 0)
                {
                    cond.wait(lk);
                    if (should_shutdown)
                    {
                        return;
                    }
                }
                if (should_shutdown)
                {
                    return;
                }
                if (work_items.size() > 0)
                {
                    work_item = work_items.front();
                    work_items.pop();
                }
            }
            if (work_item != NULL)
            {
                work_item();
            }
        }
    }

    void thread_pool::start()
    {
        assert(!running);
        running = true;
        should_shutdown = false;
        for (int i=0; i<thread_count; i++)
        {
            std::thread* t = new std::thread(&thread_pool::run, this);
            assert(t);
            threads.insert(t);
        }
    }

    void thread_pool::stop()
    {
        lock.lock();
        should_shutdown = true;
        cond.notify_all();
        std::for_each(threads.begin(), threads.end(), [](std::thread* thread) {
                pthread_kill(thread->native_handle(), SIGUSR2);
            });
        lock.unlock();
    }

    void thread_pool::wait()
    {
        assert(running);
        std::for_each(threads.begin(), threads.end(), [](std::thread* thread) {
                thread->join();
            });
        running = false;
    }

    void thread_pool::release()
    {
        std::for_each(threads.begin(), threads.end(), [](std::thread* thread) {
                delete thread;
            });
        threads.clear();
    }

    void thread_pool::queue_work_item(work_element item)
    {
        lock.lock();
        work_items.emplace(item);
        cond.notify_one();
        lock.unlock();
    }

    void thread_pool::begin_queue_work_item()
    {
        lock.lock();
    }

    void thread_pool::queue_work_item_batch(work_element item)
    {
        work_items.emplace(item);
        cond.notify_one();
    }
    void thread_pool::end_queue_work_item()
    {
        lock.unlock();
    }

}
