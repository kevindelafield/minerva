#pragma once

#include <exception>
#include <string>

namespace minerva
{

    class http_exception : public std::exception
    {
    public:

        explicit http_exception(const char* what) : _what(what ? what : "")
        {
        }

        explicit http_exception(std::string what) : _what(std::move(what))
        {
        }

        virtual const char* what() const noexcept override
        {
            return _what.c_str();
        }

    private:

        std::string _what;

    };
}

