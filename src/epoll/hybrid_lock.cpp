#include <vector>
#include <ovhttpd/log.h>
#include "hybrid_lock.h"

namespace epoll
{

    static std::vector<std::mutex> locks(hybrid_lock::LOCK_COUNT);
    std::atomic<unsigned long long> hybrid_lock::counter(0);

    void hybrid_lock::lock()
    {
        locks[lock_number].lock();
    }

    void hybrid_lock::unlock()
    {
        locks[lock_number].unlock();
    }
}
