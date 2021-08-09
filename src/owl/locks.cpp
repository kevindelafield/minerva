#include "locks.h"

namespace owl
{
    std::shared_mutex shared_lock;
    
    std::mutex global_lock;
    
    std::mutex fd_lock;
    
}
