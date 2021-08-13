#pragma once

#include <mutex>
#include <shared_mutex>

namespace owl
{
   extern std::shared_mutex shared_lock;

   extern std::mutex global_lock;

}
