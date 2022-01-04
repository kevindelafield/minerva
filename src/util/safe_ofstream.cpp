#include <unistd.h>
#include <stdio.h>
#include "unique_command.h"
#include "log.h"
#include "safe_ofstream.h"

namespace util
{
    safe_ofstream::~safe_ofstream()
    {
        if (m_open && !m_commited)
        {
            close();
            if (unlink(m_fakepath.c_str()))
            {
                LOG_ERROR_ERRNO("failed to unlink file: " << m_fakepath, errno);
            }
        }
    }

    bool safe_ofstream::commit()
    {
        if (!*this)
        {
            LOG_ERROR_ERRNO("error on stream: " << m_fakepath, errno);
            return false;
        }
        flush();
        close();

        sync();

        if (rename(m_fakepath.c_str(), m_realpath.c_str()))
        {
            LOG_ERROR_ERRNO("failed to rename file: " << m_fakepath, errno);
            return false;
        }
        m_commited = true;
        
        sync();

        return true;
    }
}
