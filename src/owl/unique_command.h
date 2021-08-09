#pragma once

#include <functional>

namespace owl
{
    class unique_command
    {
    public:
    unique_command(std::function<void()> cmd) : m_cmd(cmd)
        {
        }
        
        ~unique_command()
        {
            m_cmd();
        }

    private:
        std::function<void()> m_cmd;
    };
}
