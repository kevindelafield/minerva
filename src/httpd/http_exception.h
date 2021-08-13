#pragma once

#include <exception>

namespace httpd
{

    class http_exception : public std::exception
    {
    public:
        
        http_exception(const char* what) : _what(what)
        {
        }
        
        virtual const char* what() const throw()
        {
            return _what;
        }
        
    private:
        
        const char * _what;
        
    };
}

