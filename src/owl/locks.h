#pragma once

#include <mutex>
#include <shared_mutex>
#include <condition_variable>

namespace minerva
{
   extern std::shared_mutex shared_lock;

   extern std::mutex global_lock;

   class locks
   {
   };
}
