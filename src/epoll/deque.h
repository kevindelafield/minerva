#include <list>
#include <memory>
#include <vector>
#include <cassert>
#include <mutex>
#include <cstring>

namespace epoll
{

    class dequeue
    {
    private:
        std::list<std::shared_ptr<std::vector<char>>> _list;
        constexpr static size_t DEFAULT_CHUNK_SIZE = 10 * 1024;
        size_t _chunk_size;
        size_t _size;
        size_t _front_offset = 0;
        size_t _back_offset = 0;
        static std::mutex _lock;
        static std::list<std::shared_ptr<std::vector<char>>> _cache;

        static void add_to_cache(std::shared_ptr<std::vector<char>> buf);

        static std::shared_ptr<std::vector<char>> get_from_cache();

    public:
    dequeue(size_t chunk_size = DEFAULT_CHUNK_SIZE) : _chunk_size(chunk_size)
        {
        }

        ~dequeue() = default;

        void write(char *buf, size_t size)
        {
            std::shared_ptr<std::vector<char>> data;
            if (_list.empty())
            {
                data = get_from_cache();
                if (!data)
                {
                    data = std::make_shared<std::vector<char>>(_chunk_size);
                }
                _list.push_back(data);
            }
            else
            {
                data = _list.back();
            }
            while (size > 0)
            {
                if (_back_offset == data->size())
                {
                    data = get_from_cache();
                    if (!data)
                    {
                        data = std::make_shared<std::vector<char>>(_chunk_size);
                    }
                    _list.push_back(data);
                    _back_offset = 0;
                }
                size_t remaining = data->size() - _back_offset;
                size_t to_copy = std::min(remaining, size);
                std::memcpy(&(*data)[_back_offset], buf, to_copy);
                _size += to_copy;
                _back_offset += to_copy;
                size -= to_copy;
                assert(size >= 0);
            }
        }

        size_t read(char* buf, size_t size)
        {
            if (_list.empty())
            {
                return 0;
            }
            auto it = _list.cbegin();
            size_t read = 0;
            while (read < size && it != _list.cend())
            {
                size_t start;
                size_t stop;
                if (*it == _list.front())
                {
                    start = _front_offset;
                }
                else
                {
                    start = 0;
                }
                if (*it == _list.back())
                {
                    stop = _back_offset;
                }
                else
                {
                    stop = (*it)->size();
                }
                size_t to_copy = std::min(stop - start, size);
                std::memcpy(buf+read, &(**it)[start], to_copy);
                read += to_copy;
                it++;
            }
            return read;
        }

        void consume(size_t size)
        {
            if (!size)
            {
                return;
            }
            assert(size <= _size);
            assert(!_list.empty());
            auto data = _list.front();
            while (size > 0)
            {
                assert(data);
                size_t start = _front_offset;
                size_t stop;
                if (data == _list.back())
                {
                    stop = _back_offset;
                }
                else
                {
                    stop = data->size();
                }
                size_t to_remove = std::min(stop - start, size);
                if (start + to_remove == stop)
                {
                    add_to_cache(_list.front());
                    _list.pop_front();
                    _front_offset = 0;
                    data = _list.empty() ? nullptr : _list.front();
                }
                else
                {
                    assert(size == 0);
                    _front_offset += to_remove;
                }
                _size -= to_remove;
                size -= to_remove;
            }
        }
    };
}
