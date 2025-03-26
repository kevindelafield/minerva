#pragma once

#include <cassert>
#include <optional>

namespace minerva
{

    template<class T>
    class nillable
    {
    public:
        nillable() : m_has_value(false)
        {
        }

        nillable(const T & val) : m_value(val), m_has_value(true)
        {
        }

        nillable(const nillable & other) = default;

        nillable & operator=(const nillable & other)
        {
            if (&other != this)
            {
                if (other.m_has_value)
                {
                    m_value = other.m_value;
                }
                m_has_value = other.m_has_value;
            }
            return *this;
        }
        
        bool has_value() const
        {
            return m_has_value;
        }

        T & value()
        {
            assert(m_has_value);
            return m_value;
        }

        const T & Value() const
        {
            assert(m_has_value);
            return m_value;
        }

        void value(const T & value)
        {
            m_value = value;
            m_has_value = true;
        }

        void clear() 
        {
            m_has_value = false;
        }

    private:
        T m_value;
        bool m_has_value;
    };
}
