#pragma once

#include <set>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <functional>

namespace owl
{

    class thread_pool
    {
    public:
        typedef std::function<void()> work_element;

        thread_pool(int count);
        virtual ~thread_pool();

        void queue_work_item(work_element);

        void begin_queue_work_item();
        void queue_work_item_batch(work_element);
        void end_queue_work_item();

        void start();
        void stop();
        void wait();

    private:
        volatile bool should_shutdown;
        bool running;
        std::queue<work_element> work_items;
        std::set<std::thread*> threads;
        std::mutex lock;
        std::condition_variable cond;
        void run();
        int thread_count;
    };
}
