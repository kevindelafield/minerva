#pragma once

#include <vector>

namespace owl
{

    // Returns false on failure
    bool jo_write_jpg(std::vector<unsigned char> & output,
                      const void *data, 
                      int width, 
                      int height, 
                      int comp, 
                      int quality);

}
