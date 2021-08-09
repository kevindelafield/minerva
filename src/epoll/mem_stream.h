#pragma once

#include <istream>
#include <ostream>
#include <streambuf>

namespace epoll
{

    class IMemBuf: public std::streambuf
    {
    public:
        IMemBuf(const char* base, size_t size)
        {
            char* p(const_cast<char*>(base));
            this->setg(p, p, p + size);
        }
    };

    class IMemStream: public IMemBuf, public std::istream
    {
    public:
	IMemStream(const char* mem, size_t size) :
		IMemBuf(mem, size),
            std::istream(static_cast<std::streambuf*>(this))
            {
            }
    };

    class OMemBuf: public std::streambuf
    {
    public:
        OMemBuf(char* base, size_t size)
        {
            this->setp(base, base + size);
        }
    };

    class OMemStream: public OMemBuf, public std::ostream
    {
    public:
	OMemStream(char* base, size_t size) :
		OMemBuf(base, size),
            std::ostream(static_cast<std::streambuf*>(this))
            {
            }
    };
}
