#include "deque.h"

namespace epoll
{

    std::mutex dequeue::_lock;
    std::list<std::shared_ptr<std::vector<char>>> dequeue::_cache;

    void dequeue::add_to_cache(std::shared_ptr<std::vector<char>> buf)
    {
        std::unique_lock<std::mutex> lk(_lock);
        _cache.push_back(buf);
    }

    std::shared_ptr<std::vector<char>> dequeue::get_from_cache()
    {
        std::unique_lock<std::mutex> lk(_lock);
        if (_cache.empty())
        {
            return nullptr;
        }
        auto item = _cache.front();
        _cache.pop_front();
        return item;
    }

}
