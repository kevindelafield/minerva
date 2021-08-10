#pragma once

#include <shared_mutex>
#include "shared_file_lock.h"

namespace owl
{
   extern std::shared_mutex shared_lock;

   extern std::mutex global_lock;

}
